#include "ShaderSource.h"
#include "TextureUnits.h"
#include "rhi/WgslBindings.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>
#include <sys/stat.h>

namespace ShaderSource {
namespace {

constexpr const char* kDesktopVersionDirective = "#version 460 core";

std::vector<std::string> SplitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::string current;
    for (const char c : text) {
        if (c == '\n') {
            if (!current.empty() && current.back() == '\r') {
                current.pop_back();
            }
            lines.push_back(std::move(current));
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) {
        if (current.back() == '\r') {
            current.pop_back();
        }
        lines.push_back(std::move(current));
    }
    return lines;
}

// Returns the directive name when `line` is a preprocessor directive ("version",
// "include", …) and leaves `rest` pointing just past it.
std::string DirectiveName(const std::string& line, size_t& rest) {
    size_t i = line.find_first_not_of(" \t");
    if (i == std::string::npos || line[i] != '#') {
        return {};
    }
    i = line.find_first_not_of(" \t", i + 1);
    if (i == std::string::npos) {
        return {};
    }
    size_t end = i;
    while (end < line.size() && (std::isalnum(static_cast<unsigned char>(line[end])) || line[end] == '_')) {
        ++end;
    }
    rest = end;
    return line.substr(i, end - i);
}

// `#include "path"` → path. Anything else after `#include` is an error (no <> form).
bool ParseIncludeTarget(const std::string& line, size_t rest, std::string& target) {
    const size_t open = line.find_first_not_of(" \t", rest);
    if (open == std::string::npos || line[open] != '"') {
        return false;
    }
    const size_t close = line.find('"', open + 1);
    if (close == std::string::npos || close == open + 1) {
        return false;
    }
    const size_t trailing = line.find_first_not_of(" \t", close + 1);
    if (trailing != std::string::npos && line.compare(trailing, 2, "//") != 0) {
        return false;
    }
    target = line.substr(open + 1, close - open - 1);
    return true;
}

std::string DirectoryOf(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

// Joins and collapses "." / ".." so the once-only set sees one spelling per file.
std::string Normalize(const std::string& path) {
    std::vector<std::string> parts;
    std::stringstream stream(path);
    std::string part;
    while (std::getline(stream, part, '/')) {
        if (part.empty() || part == ".") {
            continue;
        }
        if (part == ".." && !parts.empty() && parts.back() != "..") {
            parts.pop_back();
            continue;
        }
        parts.push_back(part);
    }
    std::string joined = !path.empty() && path.front() == '/' ? "/" : "";
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) {
            joined += '/';
        }
        joined += parts[i];
    }
    return joined;
}

struct Context {
    Context(const FileReader& fileReader, Dialect shaderDialect, std::string rootDirectory)
        : reader(fileReader), dialect(shaderDialect), root(std::move(rootDirectory)) {}

    const FileReader& reader;
    Dialect dialect;
    std::string root; // directory of the top-level file; includes resolve against it
    std::vector<std::string> names;
    std::unordered_set<std::string> included;
    std::vector<std::string> stack;
    std::string code;
    std::vector<SamplerBinding> samplerBindings;
    std::string error;
#ifndef __EMSCRIPTEN__
    // SpirvWebGpu only (host-side, see below): the stage's varying locations.
    struct {
        Stage stage = Stage::Vertex;
        const std::vector<Varying>* vertexOutputs = nullptr; // a fragment stage's partner, if known
        std::vector<Varying> varyings;
        std::vector<Varying> fragmentOutputs;
        int nextVarying = 0;
    } webgpu;
#endif
};

// `layout(binding = UNIT) uniform [precision] samplerX name;` → the line without the layout,
// recording (name, UNIT). UNIT is a TU_* name from TextureUnits.h or a literal. Returns false
// (with `error`) for a binding that is not on a sampler or names an unknown unit.
bool StripSamplerBinding(std::string& line, std::vector<SamplerBinding>& bindings, std::string& error,
                         Dialect dialect) {
    const size_t first = line.find_first_not_of(" \t");
    if (first == std::string::npos || line.compare(first, 6, "layout") != 0 || line.find("binding") == std::string::npos) {
        return true;
    }
    static const std::regex pattern(
        R"(^(\s*)layout\s*\(\s*binding\s*=\s*(\w+)\s*\)\s*(uniform\s+(?:(?:lowp|mediump|highp)\s+)?(\w+)\s+(\w+)\s*;.*)$)");
    std::smatch match;
    if (!std::regex_match(line, match, pattern)) {
        return true; // some other layout(binding…) form; leave it for the GLSL compiler to judge
    }
    const std::string unitName = match[2];
    const std::string type = match[4];
    if (type.rfind("sampler", 0) != 0 && type.rfind("isampler", 0) != 0 && type.rfind("usampler", 0) != 0) {
        error = "layout(binding = …) is only for samplers here (uniform blocks are bound by Shader)";
        return false;
    }
    int unit = -1;
    if (std::all_of(unitName.begin(), unitName.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); })) {
        unit = std::stoi(unitName);
    } else if (const std::optional<TextureUnit> named = TextureUnitFromGlslName(unitName)) {
        unit = static_cast<int>(*named);
    } else {
        error = "unknown texture unit " + unitName + " (see src/gfx/TextureUnits.h)";
        return false;
    }
    bindings.push_back({match[5], unit});
#ifndef __EMSCRIPTEN__
    if (dialect == Dialect::SpirvWebGpu) {
        // Vulkan GLSL wants the binding spelled out; Tint later splits the combined sampler
        // into a texture here and a sampler at the next binding (gfx/rhi/WgslBindings.h).
        line = std::string(match[1]) + "layout(set = " + std::to_string(rhi::wgsl::kTextureGroup) +
               ", binding = " + std::to_string(rhi::wgsl::TextureBinding(static_cast<std::uint32_t>(unit))) + ") " +
               std::string(match[3]);
        return true;
    }
#else
    (void)dialect;
#endif
    line = std::string(match[1]) + std::string(match[3]);
    return true;
}

// The SpirvWebGpu dialect is the WGSL toolchain's input (tools/wgsl_shader_compiler, native), so
// a web build, which only ever compiles GLSL ES 3.00, leaves its regexes out of the binary.
#ifndef __EMSCRIPTEN__
// SpirvWebGpu: `layout(std140) uniform FrameUBO {` → `layout(std140, set = 0, binding = 0) uniform
// FrameUBO {`. GL binds blocks by name at link (Shader.cpp); Vulkan needs the numbers in the source.
bool BindUniformBlock(std::string& line, std::string& error) {
    static const std::regex pattern(R"(^(\s*)layout\s*\(\s*std140\s*\)\s*uniform\s+(\w+)(.*)$)");
    std::smatch match;
    if (!std::regex_match(line, match, pattern)) {
        return true;
    }
    const std::optional<std::uint32_t> binding = rhi::wgsl::UniformBlockBinding(std::string(match[2]));
    if (!binding) {
        error = "uniform block " + std::string(match[2]) + " has no WebGPU binding (gfx/rhi/WgslBindings.h)";
        return false;
    }
    line = std::string(match[1]) + "layout(std140, set = " + std::to_string(rhi::wgsl::kFrameGroup) +
           ", binding = " + std::to_string(*binding) + ") uniform " + std::string(match[2]) + std::string(match[3]);
    return true;
}

// Locations a varying of `type` takes: a matrix one per column (GLSL's rule, which glslang checks).
int LocationCount(const std::string& type) {
    const size_t space = type.find_last_of(" \t");
    const std::string base = space == std::string::npos ? type : type.substr(space + 1);
    if (base.size() >= 4 && base.compare(0, 3, "mat") == 0 && std::isdigit(static_cast<unsigned char>(base[3]))) {
        return base[3] - '0'; // matN and matNxM: N columns
    }
    return 1;
}

// SpirvWebGpu: a global `[flat] in|out [precision] type name;` without a layout gets its
// `layout(location = …)` (see Varying in the header). Vertex inputs must already have one.
bool AssignVaryingLocation(Context& ctx, std::string& line, std::string& error) {
    static const std::regex pattern(
        R"(^(\s*)((?:(?:flat|smooth|noperspective|centroid)\s+)*)(in|out)\s+((?:(?:lowp|mediump|highp)\s+)?\w+)\s+(\w+)\s*;(.*)$)");
    std::smatch match;
    if (line.find("layout") != std::string::npos || !std::regex_match(line, match, pattern)) {
        return true;
    }
    const std::string direction = match[3];
    const std::string name = match[5];
    int location = 0;
    if (ctx.webgpu.stage == Stage::Vertex && direction == "in") {
        error = "vertex input " + name + " needs layout(location = …) (the vertex buffer layout names it)";
        return false;
    }
    // Fragment outputs are numbered on their own (location = colour attachment).
    std::vector<Varying>& table =
        ctx.webgpu.stage == Stage::Fragment && direction == "out" ? ctx.webgpu.fragmentOutputs : ctx.webgpu.varyings;
    const auto seen = std::find_if(table.begin(), table.end(), [&](const Varying& v) { return v.name == name; });
    if (seen != table.end()) {
        location = seen->location; // the same declaration in another #ifdef branch
    } else if (ctx.webgpu.stage == Stage::Fragment && direction == "out") {
        location = static_cast<int>(ctx.webgpu.fragmentOutputs.size());
        ctx.webgpu.fragmentOutputs.push_back({name, location});
    } else if (ctx.webgpu.stage == Stage::Fragment && ctx.webgpu.vertexOutputs) {
        const auto partner = std::find_if(ctx.webgpu.vertexOutputs->begin(), ctx.webgpu.vertexOutputs->end(),
                                          [&](const Varying& v) { return v.name == name; });
        if (partner == ctx.webgpu.vertexOutputs->end()) {
            error = "fragment input " + name + " is not an output of the vertex stage it is linked with";
            return false;
        }
        location = partner->location;
        ctx.webgpu.varyings.push_back({name, location});
    } else {
        location = ctx.webgpu.nextVarying;
        ctx.webgpu.nextVarying += LocationCount(match[4]);
        ctx.webgpu.varyings.push_back({name, location});
    }
    line = std::string(match[1]) + "layout(location = " + std::to_string(location) + ") " + std::string(match[2]) +
           direction + " " + std::string(match[4]) + " " + name + ";" + std::string(match[6]);
    return true;
}

#endif // !__EMSCRIPTEN__

bool Expand(Context& ctx, const std::vector<std::string>& lines, int sourceId, size_t firstLine, int depth) {
    for (size_t i = firstLine; i < lines.size(); ++i) {
        const std::string& line = lines[i];
        const std::string where = ctx.names[static_cast<size_t>(sourceId)] + ":" + std::to_string(i + 1);
        size_t rest = 0;
        const std::string directive = DirectiveName(line, rest);

        if (directive == "version") {
            ctx.error = where + ": #version is only allowed on line 1 of a top-level shader";
            return false;
        }
        if (directive != "include") {
            std::string out = line;
            std::string bindingError;
            if (directive.empty() && !StripSamplerBinding(out, ctx.samplerBindings, bindingError, ctx.dialect)) {
                ctx.error = where + ": " + bindingError;
                return false;
            }
#ifndef __EMSCRIPTEN__
            if (directive.empty() && ctx.dialect == Dialect::SpirvWebGpu &&
                (!BindUniformBlock(out, bindingError) || !AssignVaryingLocation(ctx, out, bindingError))) {
                ctx.error = where + ": " + bindingError;
                return false;
            }
#endif
            ctx.code += out;
            ctx.code += '\n';
            continue;
        }

        std::string target;
        if (!ParseIncludeTarget(line, rest, target)) {
            ctx.error = where + ": expected #include \"relative/path.glsl\"";
            return false;
        }
        if (!target.empty() && target.front() == '/') {
            ctx.error = where + ": #include paths are relative to the shader directory, not absolute";
            return false;
        }
        const std::string resolved = Normalize(ctx.root.empty() ? target : ctx.root + "/" + target);
        for (const std::string& open : ctx.stack) {
            if (open == resolved) {
                ctx.error = where + ": include cycle through " + resolved;
                return false;
            }
        }
        if (ctx.included.count(resolved) != 0) {
            ctx.code += '\n'; // already expanded for this stage; keep the line count
            continue;
        }
        if (depth + 1 > kMaxIncludeDepth) {
            ctx.error = where + ": includes nested deeper than " + std::to_string(kMaxIncludeDepth);
            return false;
        }
        const std::optional<std::string> text = ctx.reader(resolved);
        if (!text) {
            ctx.error = where + ": cannot read included file " + resolved;
            return false;
        }

        const int childId = static_cast<int>(ctx.names.size());
        ctx.names.push_back(resolved);
        ctx.included.insert(resolved);
        ctx.stack.push_back(resolved);
        ctx.code += "#line 1 " + std::to_string(childId) + "\n";
        if (!Expand(ctx, SplitLines(*text), childId, 0, depth + 1)) {
            return false;
        }
        ctx.stack.pop_back();
        ctx.code += "#line " + std::to_string(i + 2) + " " + std::to_string(sourceId) + "\n";
    }
    return true;
}

} // namespace

Dialect NativeDialect() {
#ifdef __EMSCRIPTEN__
    return Dialect::GlslEs300;
#else
    return Dialect::Desktop460;
#endif
}

bool IsValidDefine(const std::string& name) {
    if (name.empty() || std::isdigit(static_cast<unsigned char>(name[0]))) {
        return false;
    }
    // SS_ is ShaderSource's own namespace and GL_ is reserved by GLSL.
    if (name.rfind("SS_", 0) == 0 || name.rfind("GL_", 0) == 0) {
        return false;
    }
    return std::all_of(name.begin(), name.end(), [](char c) {
        return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
    });
}

Result Preprocess(const std::string& path, Stage stage, Dialect dialect, const FileReader& reader,
                  const std::vector<std::string>& defines, const std::vector<Varying>* vertexOutputs) {
    Result result;
#ifdef __EMSCRIPTEN__
    if (dialect == Dialect::SpirvWebGpu) {
        result.error = "the SpirvWebGpu dialect is host-only (tools/wgsl_shader_compiler)";
        return result;
    }
#endif
    for (const std::string& define : defines) {
        if (!IsValidDefine(define)) {
            result.error = "invalid shader define \"" + define + "\" (expected [A-Z_][A-Z0-9_]*, not SS_*/GL_*)";
            return result;
        }
    }
    const std::string top = Normalize(path);
    const std::optional<std::string> text = reader(top);
    if (!text) {
        result.error = "cannot read shader " + top;
        return result;
    }
    const std::vector<std::string> lines = SplitLines(*text);
    size_t rest = 0;
    if (lines.empty() || DirectiveName(lines[0], rest) != "version") {
        result.error = top + ":1: a top-level shader must start with #version";
        return result;
    }

    Context ctx(reader, dialect, DirectoryOf(top));
#ifndef __EMSCRIPTEN__
    ctx.webgpu.stage = stage;
    ctx.webgpu.vertexOutputs = vertexOutputs;
#else
    (void)vertexOutputs;
#endif
    ctx.names.push_back(top);
    ctx.included.insert(top);
    ctx.stack.push_back(top);

    // GLSL ES 3.00 is a subset of desktop GLSL 4.60 (precision qualifiers included), so
    // the native build only needs a different directive; it then no longer depends on
    // ARB_ES3_compatibility. Line 1 stays line 1, so on-disk numbering is preserved.
    const bool rewrite = dialect != Dialect::GlslEs300 && lines[0].find("es") != std::string::npos;
    ctx.code += rewrite ? kDesktopVersionDirective : lines[0];
    ctx.code += '\n';
    switch (stage) {
        case Stage::Vertex: ctx.code += "#define SS_STAGE_VERTEX 1\n"; break;
        case Stage::Fragment: ctx.code += "#define SS_STAGE_FRAGMENT 1\n"; break;
        case Stage::Geometry: ctx.code += "#define SS_STAGE_GEOMETRY 1\n"; break;
    }
    ctx.code += dialect == Dialect::GlslEs300 ? "#define SS_GLSL_ES 1\n" : "#define SS_GLSL_DESKTOP 1\n";
#ifndef __EMSCRIPTEN__
    const bool vertexEpilogue = dialect == Dialect::SpirvWebGpu && stage == Stage::Vertex;
    if (dialect == Dialect::SpirvWebGpu) {
        ctx.code += "#define SS_WEBGPU 1\n";
    }
    if (vertexEpilogue) {
        ctx.code += "#define main ss_vertex_main\n"; // the shader's main; the real one is appended
    }
#endif
    for (const std::string& define : defines) {
        ctx.code += "#define " + define + " 1\n";
    }

    const std::string preamblePath = Normalize(ctx.root.empty() ? kPreambleRelativePath
                                                                : ctx.root + "/" + kPreambleRelativePath);
    const std::optional<std::string> preamble = reader(preamblePath);
    if (!preamble) {
        result.error = "cannot read shader preamble " + preamblePath;
        return result;
    }
    ctx.names.push_back(preamblePath);
    ctx.included.insert(preamblePath);
    ctx.stack.push_back(preamblePath);
    ctx.code += "#line 1 1\n";
    if (!Expand(ctx, SplitLines(*preamble), 1, 0, 1)) {
        result.error = ctx.error;
        return result;
    }
    ctx.stack.pop_back();

    ctx.code += "#line 2 0\n";
    if (!Expand(ctx, lines, 0, 1, 0)) {
        result.error = ctx.error;
        return result;
    }
#ifndef __EMSCRIPTEN__
    if (vertexEpilogue) {
        // Every shader writes GL clip space: z in [-w, w] (log depth included, common/log_depth.glsl)
        // and y up into a bottom-up framebuffer. WebGPU's depth range is [0, w], and its targets
        // are top-down, so the WebGPU backend renders y-flipped: every target then holds GL's
        // row order, gl_FragCoord and texture reads of render targets agree with GL, and only
        // the final present flips back (gfx/rhi/wgpu, docs/ARCHITECTURE.md §7.9).
        ctx.code += "#undef main\n"
                    "void main() {\n"
                    "    ss_vertex_main();\n"
                    "    gl_Position.y = -gl_Position.y;\n"
                    "    gl_Position.z = 0.5 * (gl_Position.z + gl_Position.w);\n"
                    "}\n";
    }
#endif

    result.ok = true;
    result.code = std::move(ctx.code);
    result.sourceNames = std::move(ctx.names);
    result.samplerBindings = std::move(ctx.samplerBindings);
#ifndef __EMSCRIPTEN__
    result.varyings = std::move(ctx.webgpu.varyings);
    result.fragmentOutputs = std::move(ctx.webgpu.fragmentOutputs);
#endif
    return result;
}

std::optional<std::string> ReadFileFromDisk(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    std::ostringstream stream;
    stream << file.rdbuf();
    return stream.str();
}

std::optional<long long> FileModificationTime(const std::string& path) {
    struct stat info {};
    if (stat(path.c_str(), &info) != 0) {
        return std::nullopt;
    }
    // Sub-second where the platform has it: an editor can save twice within a second.
#if defined(__APPLE__)
    return static_cast<long long>(info.st_mtimespec.tv_sec) * 1000000000LL + info.st_mtimespec.tv_nsec;
#elif defined(_WIN32)
    return static_cast<long long>(info.st_mtime);
#else
    return static_cast<long long>(info.st_mtim.tv_sec) * 1000000000LL + info.st_mtim.tv_nsec;
#endif
}

namespace {
// The words after `marker` on every line that starts with it.
std::vector<std::string> MarkerWords(const std::string& text, const char* marker) {
    std::vector<std::string> words;
    for (const std::string& line : SplitLines(text)) {
        const size_t first = line.find_first_not_of(" \t");
        if (first == std::string::npos || line.compare(first, std::strlen(marker), marker) != 0) {
            continue;
        }
        std::istringstream stream(line.substr(first + std::strlen(marker)));
        std::string word;
        while (stream >> word) {
            words.push_back(word);
        }
    }
    return words;
}
} // namespace

std::vector<std::string> DeclaredVertexPartners(const std::string& text) {
    // `// vertex: a.vs b.vs (why)`: the file names, up to the first word that is not one.
    std::vector<std::string> partners;
    for (const std::string& word : MarkerWords(text, "// vertex:")) {
        if (word.size() <= 3 || word.compare(word.size() - 3, 3, ".vs") != 0 ||
            !std::all_of(word.begin(), word.end() - 3,
                         [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; })) {
            break;
        }
        partners.push_back(word);
    }
    return partners;
}

std::vector<std::string> DeclaredPermutations(const std::string& text) {
    return MarkerWords(text, "// permutations:");
}

std::string DescribeSources(const Result& result) {
    std::string description;
    for (size_t i = 0; i < result.sourceNames.size(); ++i) {
        description += "  source " + std::to_string(i) + ": " + result.sourceNames[i] + "\n";
    }
    return description;
}

std::string NumberedListing(const std::string& code) {
    std::string listing;
    const std::vector<std::string> lines = SplitLines(code);
    for (size_t i = 0; i < lines.size(); ++i) {
        listing += std::to_string(i + 1) + "\t" + lines[i] + "\n";
    }
    return listing;
}

} // namespace ShaderSource
