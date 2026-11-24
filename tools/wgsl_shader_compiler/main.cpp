// GLSL → SPIR-V → WGSL for every shader stage and every subset of its declared permutations
// (docs/ARCHITECTURE.md §7.9). Native only, GL-free.
//
//   wgsl_shader_compiler --shaders resource/shaders --out <dir> --glslang <exe> --tint <exe>
//                        [--only <file.vs|file.fs>] [--keep] [--quiet]
//
// Each stage is assembled by ShaderSource in its SpirvWebGpu dialect — exactly the text the GL
// build compiles, with explicit Vulkan bindings (gfx/rhi/WgslBindings.h) and the clip-space
// fix-up wrapped round a vertex main — then glslang (-V -R: loose uniforms are packed into one
// block per stage, group 2 bindings 0/1) writes SPIR-V and Tint's SPIR-V reader (which also splits every
// combined sampler) writes WGSL. Out come <file>[.<DEFINE>…].wgsl and manifest.json, which lists
// every stage permutation with its bindings and the byte offset of each loose uniform (from
// glslang's reflection), so the WebGPU backend can fill the block without parsing WGSL.
//
// The exit status is the number of stage permutations that failed (capped at 125), so a CMake
// custom command and the ShaderWgsl ctest fail on any regression. --keep leaves the .glsl and
// .spv beside the output for debugging.

#include "gfx/ShaderSource.h"
#include "gfx/rhi/WgslBindings.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct Options {
    fs::path shaders = "resource/shaders";
    fs::path out;
    std::string glslang = "glslang";
    std::string tint = "tint";
    std::string only;
    bool keep = false;
    bool quiet = false;
};

struct Member {
    std::string name;
    std::string type; // glslang's GL type name: float, vec3, mat4, int, bool…
    long offset = -1;
    long arraySize = 1;
};

struct Binding {
    std::uint32_t group = 0;
    std::uint32_t binding = 0;
    std::string kind; // "uniform-buffer", "texture", "sampler"
    std::string type; // the WGSL type: "texture_cube<f32>", "sampler", "SsUniforms_1"
    std::string name;
    int unit = -1; // texture/sampler: the TextureUnit it came from
};

struct StageResult {
    std::string file;   // "planetLighting.fs"
    std::string stage;  // "vertex" | "fragment"
    std::string vertex; // fragment: the vertex stage its input locations follow
    std::vector<ShaderSource::Varying> varyings;
    std::size_t colorOutputs = 0; // fragment: 0 for a depth-only stage
    std::vector<std::string> defines;
    std::string output; // "planetLighting.fs.HAS_CLOUDS.wgsl"
    bool ok = false;
    std::string error;
    std::string skipped; // `// webgpu:` reason: not translated on purpose
    long uniformBlockSize = 0;
    std::vector<Member> uniforms;
    std::vector<Binding> bindings;
    std::vector<std::pair<int, std::string>> attributes; // vertex: (location, WGSL type)
};

/** Run `command`, capture stdout+stderr, return its exit status. */
int Run(const std::string& command, std::string& output) {
    output.clear();
    FILE* pipe = popen((command + " 2>&1").c_str(), "r");
    if (!pipe) {
        output = "cannot start: " + command;
        return -1;
    }
    std::array<char, 4096> buffer{};
    while (const std::size_t read = std::fread(buffer.data(), 1, buffer.size(), pipe)) {
        output.append(buffer.data(), read);
    }
    const int status = pclose(pipe);
#ifdef WEXITSTATUS
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#else
    return status;
#endif
}

std::string Quote(const std::string& text) {
    return "'" + text + "'";
}

std::string JsonEscape(const std::string& text) {
    std::string out;
    for (const char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            case '\r': break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char hex[8];
                    std::snprintf(hex, sizeof hex, "\\u%04x", c);
                    out += hex;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

bool WriteFile(const fs::path& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary);
    file << text;
    return static_cast<bool>(file);
}

/**
 * The loose uniforms glslang packed into kUniformBlockName, from its -q reflection dump:
 *   Uniform reflection:
 *   fadeColor: offset 0, type 8b52, size 1, index 0, binding -1, stages 16
 *   Uniform block reflection:
 *   SsUniforms: offset -1, type ffffffff, size 16, index 0, binding 0, stages 16, numMembers 1
 * A uniform's `index` is the block it belongs to (FrameUBO's members are listed too). The type
 * is a GL enum in hex and `size` the array length; the backend's packer looks members up by
 * the name Shader::Set* passes and writes them at `offset`.
 */
void ParseReflection(const std::string& dump, StageResult& result) {
    static const std::regex entry(R"(^(\w+)(\[0\])?: offset (-?\d+), type ([0-9a-fx]+), size (\d+), index (-?\d+).*$)");
    std::istringstream lines(dump);
    std::string line;
    std::string section;
    std::vector<std::pair<Member, long>> uniforms; // member, block index
    long blockIndex = -1;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.find("reflection:") != std::string::npos) {
            section = line;
            continue;
        }
        std::smatch match;
        if (!std::regex_match(line, match, entry)) {
            continue;
        }
        if (section.rfind("Uniform reflection", 0) == 0) {
            Member member;
            member.name = match[1];
            member.offset = std::stol(match[3]);
            member.type = match[4];
            member.arraySize = std::stol(match[5]);
            uniforms.emplace_back(member, std::stol(match[6]));
        } else if (section.rfind("Uniform block reflection", 0) == 0 && match[1] == rhi::wgsl::kUniformBlockName) {
            result.uniformBlockSize = std::stol(match[5]);
            blockIndex = std::stol(match[6]);
        }
    }
    for (const auto& [member, index] : uniforms) {
        if (blockIndex >= 0 && index == blockIndex && member.offset >= 0) {
            result.uniforms.push_back(member);
        }
    }
    std::sort(result.uniforms.begin(), result.uniforms.end(),
              [](const Member& a, const Member& b) { return a.offset < b.offset; });
}

/** @group(g) @binding(b) var<…> name : type; lines of Tint's WGSL. */
void ParseWgslBindings(const std::string& wgsl, const ShaderSource::Result& source, StageResult& result) {
    static const std::regex binding(
        R"(@group\((\d+)u?\)\s*@binding\((\d+)u?\)\s*var(<[^>]*>)?\s+(\w+)\s*:\s*([\w<>, ]+);)");
    for (auto it = std::sregex_iterator(wgsl.begin(), wgsl.end(), binding); it != std::sregex_iterator(); ++it) {
        Binding entry;
        entry.group = static_cast<std::uint32_t>(std::stoul((*it)[1]));
        entry.binding = static_cast<std::uint32_t>(std::stoul((*it)[2]));
        entry.name = (*it)[4];
        std::string type = (*it)[5];
        type.erase(type.find_last_not_of(' ') + 1);
        entry.type = type;
        if (type.rfind("sampler", 0) == 0) {
            entry.kind = "sampler";
        } else if (type.rfind("texture", 0) == 0) {
            entry.kind = "texture";
        } else {
            entry.kind = "uniform-buffer";
        }
        if (entry.group == rhi::wgsl::kTextureGroup) {
            entry.unit = static_cast<int>(entry.binding / 2);
        }
        result.bindings.push_back(entry);
    }
    // The vertex entry point's attributes: `@location(0) aPos : vec3<f32>` parameters of `@vertex fn`.
    const std::size_t entry = wgsl.find("@vertex");
    if (entry != std::string::npos) {
        const std::size_t open = wgsl.find('(', entry);
        const std::size_t close = wgsl.find(")", wgsl.find("fn ", entry));
        // The parameter list ends at the first ") ->" or ") {" after the fn keyword.
        const std::size_t end = std::min(wgsl.find(") ->", open), wgsl.find(") {", open));
        const std::string params = wgsl.substr(open, (end == std::string::npos ? close : end) - open);
        static const std::regex attribute(R"(@location\((\d+)u?\)\s*(\w+)\s*:\s*([\w<>]+))");
        for (auto it = std::sregex_iterator(params.begin(), params.end(), attribute); it != std::sregex_iterator();
             ++it) {
            result.attributes.emplace_back(std::stoi((*it)[1]), (*it)[3]);
        }
    }
    // Every sampler the source declared must sit where WgslBindings.h says, texture and sampler.
    for (const ShaderSource::SamplerBinding& sampler : source.samplerBindings) {
        const auto unit = static_cast<std::uint32_t>(sampler.unit);
        const auto at = [&](std::uint32_t number, const char* kind) {
            return std::any_of(result.bindings.begin(), result.bindings.end(), [&](const Binding& b) {
                return b.group == rhi::wgsl::kTextureGroup && b.binding == number && b.kind == kind;
            });
        };
        const bool used = wgsl.find(sampler.uniform) != std::string::npos;
        if (used &&
            (!at(rhi::wgsl::TextureBinding(unit), "texture") || !at(rhi::wgsl::SamplerBinding(unit), "sampler"))) {
            result.ok = false;
            result.error += "sampler " + sampler.uniform + " (unit " + std::to_string(unit) +
                            ") did not land on @group(1) @binding(" + std::to_string(rhi::wgsl::TextureBinding(unit)) +
                            "/" + std::to_string(rhi::wgsl::SamplerBinding(unit)) + ")\n";
        }
    }
}

/**
 * `// webgpu: <reason>` in a stage marks it as one the WebGPU backend replaces rather than
 * translates (point sprites: WebGPU rasterises 1-pixel points only, and has no gl_PointCoord).
 * It is reported and listed as skipped, not failed.
 */
std::optional<std::string> WebGpuSkipReason(const std::string& text) {
    static constexpr const char* kMarker = "// webgpu:";
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        const std::size_t first = line.find_first_not_of(" \t");
        if (first != std::string::npos && line.compare(first, std::strlen(kMarker), kMarker) == 0) {
            std::string reason = line.substr(first + std::strlen(kMarker));
            reason.erase(0, reason.find_first_not_of(" \t"));
            return reason;
        }
    }
    return std::nullopt;
}

std::vector<fs::path> TopLevelStages(const fs::path& directory) {
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(directory)) {
        const std::string extension = entry.path().extension().string();
        if (entry.is_regular_file() && (extension == ".vs" || extension == ".fs")) {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

/**
 * The WGSL file a stage permutation is written to: "<stage>[.<DEFINE>…].wgsl", and a fragment
 * stage linked behind a vertex stage of another name is "<fs>@<vs>[.<DEFINE>…].wgsl"
 * (hdr.fs@passThrough.vs.wgsl). gfx/rhi/wgpu derives the same name from a ShaderSpec.
 */
std::string OutputName(const std::string& file, const std::string& vertexPartner,
                       const std::vector<std::string>& defines) {
    std::string base = file;
    const std::string stem = file.substr(0, file.rfind('.'));
    if (!vertexPartner.empty() && vertexPartner.substr(0, vertexPartner.rfind('.')) != stem) {
        base += "@" + vertexPartner;
    }
    for (const std::string& define : defines) {
        base += "." + define;
    }
    return base;
}

/** A fragment stage's partners: <stem>.vs if there is one, then its `// vertex:` list. */
std::vector<std::string> VertexPartners(const fs::path& fragment, const std::string& text) {
    std::vector<std::string> partners;
    const fs::path sameStem = fragment.parent_path() / (fragment.stem().string() + ".vs");
    if (fs::exists(sameStem)) {
        partners.push_back(sameStem.filename().string());
    }
    for (const std::string& partner : ShaderSource::DeclaredVertexPartners(text)) {
        if (std::find(partners.begin(), partners.end(), partner) == partners.end()) {
            partners.push_back(partner);
        }
    }
    return partners;
}

StageResult CompileStage(const Options& options, const fs::path& file, const std::vector<std::string>& defines,
                         const fs::path& work, const std::string& vertexPartner = {}) {
    StageResult result;
    result.file = file.filename().string();
    const bool vertex = file.extension() == ".vs";
    result.stage = vertex ? "vertex" : "fragment";
    result.vertex = vertexPartner;
    result.defines = defines;
    const std::string base = OutputName(result.file, vertexPartner, defines);
    result.output = base + ".wgsl";

    // A fragment stage's input locations are its partner's output locations, which do not
    // depend on the defines (ShaderSource numbers every declaration), so no defines here.
    std::vector<ShaderSource::Varying> partnerOutputs;
    if (!vertex) {
        const ShaderSource::Result partner =
            ShaderSource::Preprocess((file.parent_path() / vertexPartner).generic_string(), ShaderSource::Stage::Vertex,
                                     ShaderSource::Dialect::SpirvWebGpu, ShaderSource::ReadFileFromDisk);
        if (!partner.ok) {
            result.error = "partner " + vertexPartner + ": " + partner.error;
            return result;
        }
        partnerOutputs = partner.varyings;
    }
    const ShaderSource::Result source = ShaderSource::Preprocess(
        file.generic_string(), vertex ? ShaderSource::Stage::Vertex : ShaderSource::Stage::Fragment,
        ShaderSource::Dialect::SpirvWebGpu, ShaderSource::ReadFileFromDisk, defines,
        vertex ? nullptr : &partnerOutputs);
    if (!source.ok) {
        result.error = "preprocess: " + source.error;
        return result;
    }
    result.varyings = source.varyings;
    result.colorOutputs = source.fragmentOutputs.size();
    const fs::path glsl = work / (base + ".glsl");
    const fs::path spv = work / (base + ".spv");
    const fs::path wgsl = options.out / result.output;
    if (!WriteFile(glsl, source.code)) {
        result.error = "cannot write " + glsl.string();
        return result;
    }

    std::string log;
    const std::string glslangArgs =
        std::string(" -V -R --target-env vulkan1.1 -S ") + (vertex ? "vert" : "frag") +
        " --set-default-uniform-block " + rhi::wgsl::kUniformBlockName + " " +
        std::to_string(rhi::wgsl::kUniformGroup) + " " +
        std::to_string(vertex ? rhi::wgsl::kVertexUniformBinding : rhi::wgsl::kFragmentUniformBinding) + " ";
    if (Run(Quote(options.glslang) + glslangArgs + "-o " + Quote(spv.string()) + " " + Quote(glsl.string()), log) !=
        0) {
        result.error = "glslang: " + log;
        return result;
    }
    std::string reflection;
    // -q prints the reflection; -o keeps its SPIR-V out of the working directory.
    const fs::path reflected = work / (base + ".reflect.spv");
    if (Run(Quote(options.glslang) + glslangArgs + "-l -q -o " + Quote(reflected.string()) + " " + Quote(glsl.string()),
            reflection) == 0) {
        ParseReflection(reflection, result);
    }
    fs::remove(reflected);
    // Tint splits every combined sampler into a texture (keeping the binding) and a sampler;
    // without a mapping it would put the samplers wherever bindings are free, so map each unit's
    // to the binding after its texture (gfx/rhi/WgslBindings.h). Non-uniform derivatives are
    // allowed because GLSL samples with implicit derivatives in divergent flow everywhere and
    // WGSL's uniformity analysis would reject that (the results are the same as GL's).
    //
    // The group is written "01", not "1": Tint's CLI reads the binding of a "group,binding" pair
    // with the group's digit count (tint/cmd/tint/main.cc, str_to_bp), so with a one-digit group
    // binding 10 would be read as 1. Every binding here is below 100, so two digits are enough.
    std::string samplerMapping;
    for (const ShaderSource::SamplerBinding& sampler : source.samplerBindings) {
        const auto unit = static_cast<std::uint32_t>(sampler.unit);
        static_assert(rhi::wgsl::kTextureGroup < 10, "the padding below assumes a one-digit group");
        const std::string group = "0" + std::to_string(rhi::wgsl::kTextureGroup);
        samplerMapping += (samplerMapping.empty() ? "" : " ") + group + "," +
                          std::to_string(rhi::wgsl::TextureBinding(unit)) + ":" + group + "," +
                          std::to_string(rhi::wgsl::SamplerBinding(unit));
    }
    const std::string tintArgs =
        " --format wgsl --allow-non-uniform-derivatives true" +
        (samplerMapping.empty() ? std::string() : " --sampler-mapping " + Quote(samplerMapping));
    if (Run(Quote(options.tint) + tintArgs + " -o " + Quote(wgsl.string()) + " " + Quote(spv.string()), log) != 0) {
        result.error = "tint: " + log;
        return result;
    }
    const std::optional<std::string> text = ShaderSource::ReadFileFromDisk(wgsl.string());
    if (!text) {
        result.error = "tint wrote no " + wgsl.string();
        return result;
    }
    result.ok = true;
    ParseWgslBindings(*text, source, result);
    if (!options.keep) {
        fs::remove(glsl);
        fs::remove(spv);
    }
    return result;
}

std::string ManifestJson(const std::vector<StageResult>& results) {
    std::ostringstream json;
    json << "{\n  \"version\": 1,\n  \"groups\": {\"frame\": " << rhi::wgsl::kFrameGroup
         << ", \"textures\": " << rhi::wgsl::kTextureGroup << ", \"uniforms\": " << rhi::wgsl::kUniformGroup
         << "},\n  \"stages\": [\n";
    for (std::size_t i = 0; i < results.size(); ++i) {
        const StageResult& r = results[i];
        json << "    {\"file\": \"" << JsonEscape(r.file) << "\", \"stage\": \"" << r.stage << "\", \"defines\": [";
        for (std::size_t d = 0; d < r.defines.size(); ++d) {
            json << (d ? ", " : "") << "\"" << r.defines[d] << "\"";
        }
        json << "], \"wgsl\": \"" << JsonEscape(r.output) << "\", \"ok\": " << (r.ok ? "true" : "false");
        if (!r.vertex.empty()) {
            json << ", \"vertex\": \"" << JsonEscape(r.vertex) << "\", \"colorOutputs\": " << r.colorOutputs;
        }
        json << ", \"varyings\": {";
        for (std::size_t v = 0; v < r.varyings.size(); ++v) {
            json << (v ? ", " : "") << "\"" << r.varyings[v].name << "\": " << r.varyings[v].location;
        }
        json << "}";
        if (!r.skipped.empty()) {
            json << ", \"skipped\": \"" << JsonEscape(r.skipped) << "\"";
        } else if (!r.ok) {
            json << ", \"error\": \"" << JsonEscape(r.error) << "\"";
        }
        json << ",\n     \"uniformBlockSize\": " << r.uniformBlockSize << ", \"uniforms\": [";
        for (std::size_t u = 0; u < r.uniforms.size(); ++u) {
            const Member& m = r.uniforms[u];
            json << (u ? ", " : "") << "{\"name\": \"" << m.name << "\", \"offset\": " << m.offset << ", \"type\": \""
                 << m.type << "\", \"count\": " << m.arraySize << "}";
        }
        json << "],\n     \"attributes\": [";
        for (std::size_t a = 0; a < r.attributes.size(); ++a) {
            json << (a ? ", " : "") << "{\"location\": " << r.attributes[a].first << ", \"type\": \""
                 << r.attributes[a].second << "\"}";
        }
        json << "],\n     \"bindings\": [";
        for (std::size_t b = 0; b < r.bindings.size(); ++b) {
            const Binding& e = r.bindings[b];
            json << (b ? ", " : "") << "{\"group\": " << e.group << ", \"binding\": " << e.binding << ", \"kind\": \""
                 << e.kind << "\", \"type\": \"" << JsonEscape(e.type) << "\", \"name\": \"" << e.name << "\"";
            if (e.unit >= 0) {
                json << ", \"unit\": " << e.unit;
            }
            json << "}";
        }
        json << "]}" << (i + 1 < results.size() ? "," : "") << "\n";
    }
    json << "  ]\n}\n";
    return json.str();
}

bool ParseArgs(int argc, char** argv, Options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto value = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
        if (arg == "--shaders") {
            options.shaders = value();
        } else if (arg == "--out") {
            options.out = value();
        } else if (arg == "--glslang") {
            options.glslang = value();
        } else if (arg == "--tint") {
            options.tint = value();
        } else if (arg == "--only") {
            options.only = value();
        } else if (arg == "--keep") {
            options.keep = true;
        } else if (arg == "--quiet") {
            options.quiet = true;
        } else {
            std::cerr << "unknown argument " << arg << "\n";
            return false;
        }
    }
    return !options.out.empty();
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    if (!ParseArgs(argc, argv, options)) {
        std::cerr << "usage: wgsl_shader_compiler --shaders <dir> --out <dir> [--glslang exe] [--tint exe]"
                     " [--only file] [--keep] [--quiet]\n";
        return 2;
    }
    std::error_code ec;
    fs::create_directories(options.out, ec);
    const fs::path work = options.out / ".work";
    fs::create_directories(work, ec);

    std::vector<StageResult> results;
    int failures = 0;
    int skippedCount = 0;
    for (const fs::path& file : TopLevelStages(options.shaders)) {
        if (!options.only.empty() && file.filename() != options.only) {
            continue;
        }
        const std::optional<std::string> text = ShaderSource::ReadFileFromDisk(file.string());
        if (const std::optional<std::string> reason = WebGpuSkipReason(text.value_or(""))) {
            StageResult skipped;
            skipped.file = file.filename().string();
            skipped.stage = file.extension() == ".vs" ? "vertex" : "fragment";
            skipped.output = OutputName(skipped.file, {}, {}) + ".wgsl";
            skipped.skipped = *reason;
            ++skippedCount;
            std::cout << "[ShaderWgsl] skipped " << skipped.file << ": " << *reason << "\n";
            results.push_back(std::move(skipped));
            continue;
        }
        const std::vector<std::string> permutations = ShaderSource::DeclaredPermutations(text.value_or(""));
        for (unsigned mask = 0; mask < (1u << permutations.size()); ++mask) {
            std::vector<std::string> defines;
            for (std::size_t bit = 0; bit < permutations.size(); ++bit) {
                if (mask & (1u << bit)) {
                    defines.push_back(permutations[bit]);
                }
            }
            std::sort(defines.begin(), defines.end()); // ShaderSpec keeps its defines sorted
            std::vector<std::string> partners{std::string()};
            if (file.extension() == ".fs") {
                partners = VertexPartners(file, text.value_or(""));
                if (partners.empty()) {
                    StageResult orphan;
                    orphan.file = file.filename().string();
                    orphan.stage = "fragment";
                    orphan.output = OutputName(orphan.file, {}, defines) + ".wgsl";
                    orphan.error = "no vertex partner: add " + file.stem().string() + ".vs or a `// vertex: x.vs` line";
                    partners.clear();
                    ++failures;
                    std::cerr << "[ShaderWgsl] FAILED " << orphan.output << "\n" << orphan.error << "\n";
                    results.push_back(std::move(orphan));
                }
            }
            for (const std::string& partner : partners) {
                StageResult result = CompileStage(options, file, defines, work, partner);
                if (!result.ok) {
                    ++failures;
                    std::cerr << "[ShaderWgsl] FAILED " << result.output << "\n" << result.error << "\n";
                } else if (!options.quiet) {
                    std::cout << "[ShaderWgsl] " << result.output << "\n";
                }
                results.push_back(std::move(result));
            }
        }
    }
    if (!options.keep) {
        fs::remove_all(work, ec);
    }
    if (!WriteFile(options.out / "manifest.json", ManifestJson(results))) {
        std::cerr << "cannot write manifest\n";
        return 125;
    }
    const std::size_t attempted = results.size() - static_cast<std::size_t>(skippedCount);
    std::cout << "[ShaderWgsl] " << (attempted - static_cast<std::size_t>(failures)) << "/" << attempted
              << " stage permutations translated to WGSL, " << skippedCount << " stage(s) replaced on WebGPU"
              << (failures ? " - FAILURES" : " OK") << "\n";
    return std::min(failures, 125);
}
