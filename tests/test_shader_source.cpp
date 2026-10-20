// Covers ShaderSource: the #version rewrite, the preamble, #include expansion, sampler
// `binding =` extraction, permutation defines and the #line bookkeeping that keeps compiler
// errors pointing at the right file and line; and CatalogMaterial's variant masks. The last
// tests run every real shader in resource/shaders through the preprocessor.

#include "gfx/ShaderSource.h"
#include "Solar_System/CatalogMaterial.h"
#include "gfx/TextureUnits.h"

#include <algorithm>
#include <gtest/gtest.h>

#include <filesystem>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>

namespace {

using ShaderSource::Dialect;
using ShaderSource::Preprocess;
using ShaderSource::Stage;

struct MemoryFiles {
    std::map<std::string, std::string> files;

    ShaderSource::FileReader Reader() const {
        return [this](const std::string& path) -> std::optional<std::string> {
            const auto it = files.find(path);
            if (it == files.end()) {
                return std::nullopt;
            }
            return it->second;
        };
    }
};

MemoryFiles BaseFiles() {
    MemoryFiles fs;
    fs.files["shaders/common/preamble.glsl"] = "#ifdef SS_STAGE_FRAGMENT\nprecision highp float;\n#endif\n";
    return fs;
}

size_t CountOf(const std::string& haystack, const std::string& needle) {
    size_t count = 0;
    for (size_t at = haystack.find(needle); at != std::string::npos; at = haystack.find(needle, at + 1)) {
        ++count;
    }
    return count;
}

TEST(ShaderSource, KeepsVersionFirstThenPreambleThenTopFileAtLineTwo) {
    MemoryFiles fs = BaseFiles();
    fs.files["shaders/a.fs"] = "#version 300 es\nout vec4 c;\nvoid main() { c = vec4(1.0); }\n";

    const auto result = Preprocess("shaders/a.fs", Stage::Fragment, Dialect::GlslEs300, fs.Reader());
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.code.rfind("#version 300 es\n", 0), 0u);
    EXPECT_NE(result.code.find("#define SS_STAGE_FRAGMENT 1\n"), std::string::npos);
    EXPECT_NE(result.code.find("#define SS_GLSL_ES 1\n"), std::string::npos);

    const size_t preamble = result.code.find("#line 1 1\n#ifdef SS_STAGE_FRAGMENT");
    const size_t top = result.code.find("#line 2 0\nout vec4 c;");
    ASSERT_NE(preamble, std::string::npos);
    ASSERT_NE(top, std::string::npos);
    EXPECT_LT(preamble, top);

    ASSERT_EQ(result.sourceNames.size(), 2u);
    EXPECT_EQ(result.sourceNames[0], "shaders/a.fs");
    EXPECT_EQ(result.sourceNames[1], "shaders/common/preamble.glsl");
}

TEST(ShaderSource, DesktopDialectRewritesEsVersionOnly) {
    MemoryFiles fs = BaseFiles();
    fs.files["shaders/a.vs"] = "#version 300 es\nvoid main() {}\n";
    fs.files["shaders/b.vs"] = "#version 330 core\nvoid main() {}\n";

    const auto es = Preprocess("shaders/a.vs", Stage::Vertex, Dialect::Desktop460, fs.Reader());
    ASSERT_TRUE(es.ok) << es.error;
    EXPECT_EQ(es.code.rfind("#version 460 core\n", 0), 0u);
    EXPECT_NE(es.code.find("#define SS_STAGE_VERTEX 1\n"), std::string::npos);
    EXPECT_NE(es.code.find("#define SS_GLSL_DESKTOP 1\n"), std::string::npos);

    const auto desktop = Preprocess("shaders/b.vs", Stage::Vertex, Dialect::Desktop460, fs.Reader());
    ASSERT_TRUE(desktop.ok) << desktop.error;
    EXPECT_EQ(desktop.code.rfind("#version 330 core\n", 0), 0u);
}

TEST(ShaderSource, ExpandsIncludesWithLineDirectivesBackToTheParent) {
    MemoryFiles fs = BaseFiles();
    fs.files["shaders/common/x.glsl"] = "float x() { return 1.0; }\nfloat x2() { return 2.0; }\n";
    fs.files["shaders/a.fs"] =
        "#version 300 es\n"
        "out vec4 c;\n"
        "#include \"common/x.glsl\" // trailing comment is fine\n"
        "void main() { c = vec4(x()); }\n";

    const auto result = Preprocess("shaders/a.fs", Stage::Fragment, Dialect::GlslEs300, fs.Reader());
    ASSERT_TRUE(result.ok) << result.error;
    ASSERT_EQ(result.sourceNames.size(), 3u);
    EXPECT_EQ(result.sourceNames[2], "shaders/common/x.glsl");
    // Included file starts at its own line 1; the parent resumes at the line after the include.
    EXPECT_NE(result.code.find("#line 1 2\nfloat x() { return 1.0; }\nfloat x2() { return 2.0; }\n#line 4 0\n"
                               "void main()"),
              std::string::npos)
        << result.code;
    EXPECT_EQ(result.code.find("#include"), std::string::npos);
}

TEST(ShaderSource, ExpandsEachFileOncePerStageAndKeepsLineCount) {
    MemoryFiles fs = BaseFiles();
    fs.files["shaders/common/base.glsl"] = "float base() { return 1.0; }\n";
    fs.files["shaders/common/mid.glsl"] = "#include \"common/base.glsl\"\nfloat mid() { return base(); }\n";
    fs.files["shaders/a.fs"] =
        "#version 300 es\n"
        "#include \"common/base.glsl\"\n"
        "#include \"common/mid.glsl\"\n"
        "#include \"common/base.glsl\"\n"
        "void main() {}\n";

    const auto result = Preprocess("shaders/a.fs", Stage::Fragment, Dialect::GlslEs300, fs.Reader());
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(CountOf(result.code, "float base()"), 1u);
    EXPECT_EQ(CountOf(result.code, "float mid()"), 1u);
    // The repeated include on line 4 becomes a blank line, so main() is still line 5.
    EXPECT_NE(result.code.find("#line 4 0\n\nvoid main() {}"), std::string::npos) << result.code;
}

TEST(ShaderSource, IncludesResolveAgainstTheShaderDirectoryAndNormalize) {
    MemoryFiles fs = BaseFiles();
    fs.files["shaders/common/a.glsl"] = "#include \"common/../common/b.glsl\"\nfloat a() { return b(); }\n";
    fs.files["shaders/common/b.glsl"] = "float b() { return 1.0; }\n";
    fs.files["shaders/top.vs"] = "#version 300 es\n#include \"./common/a.glsl\"\n#include \"common/b.glsl\"\nvoid main() {}\n";

    const auto result = Preprocess("shaders/top.vs", Stage::Vertex, Dialect::GlslEs300, fs.Reader());
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(CountOf(result.code, "float b()"), 1u);
}

TEST(ShaderSource, RejectsCyclesMissingFilesNestedVersionAndBadSyntax) {
    MemoryFiles fs = BaseFiles();
    fs.files["shaders/common/loop1.glsl"] = "#include \"common/loop2.glsl\"\n";
    fs.files["shaders/common/loop2.glsl"] = "#include \"common/loop1.glsl\"\n";
    fs.files["shaders/common/versioned.glsl"] = "#version 300 es\n";
    fs.files["shaders/cycle.fs"] = "#version 300 es\n#include \"common/loop1.glsl\"\n";
    fs.files["shaders/missing.fs"] = "#version 300 es\n#include \"common/nope.glsl\"\n";
    fs.files["shaders/nested.fs"] = "#version 300 es\n#include \"common/versioned.glsl\"\n";
    fs.files["shaders/angle.fs"] = "#version 300 es\n#include <common/x.glsl>\n";
    fs.files["shaders/absolute.fs"] = "#version 300 es\n#include \"/etc/passwd\"\n";
    fs.files["shaders/noversion.fs"] = "precision highp float;\n";

    const auto check = [&](const char* path, const char* expected) {
        const auto result = Preprocess(path, Stage::Fragment, Dialect::GlslEs300, fs.Reader());
        EXPECT_FALSE(result.ok) << path;
        EXPECT_NE(result.error.find(expected), std::string::npos) << path << ": " << result.error;
    };
    check("shaders/cycle.fs", "include cycle");
    check("shaders/missing.fs", "cannot read included file shaders/common/nope.glsl");
    check("shaders/nested.fs", "#version is only allowed");
    check("shaders/angle.fs", "expected #include");
    check("shaders/absolute.fs", "not absolute");
    check("shaders/noversion.fs", "must start with #version");
    check("shaders/does_not_exist.fs", "cannot read shader");
}

TEST(ShaderSource, RejectsIncludesNestedTooDeep) {
    MemoryFiles fs = BaseFiles();
    for (int i = 0; i <= ShaderSource::kMaxIncludeDepth; ++i) {
        fs.files["shaders/common/d" + std::to_string(i) + ".glsl"] =
            "#include \"common/d" + std::to_string(i + 1) + ".glsl\"\n";
    }
    fs.files["shaders/common/d" + std::to_string(ShaderSource::kMaxIncludeDepth + 1) + ".glsl"] = "\n";
    fs.files["shaders/deep.fs"] = "#version 300 es\n#include \"common/d0.glsl\"\n";

    const auto result = Preprocess("shaders/deep.fs", Stage::Fragment, Dialect::GlslEs300, fs.Reader());
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error.find("nested deeper"), std::string::npos) << result.error;
}

TEST(ShaderSource, MissingPreambleIsAnError) {
    MemoryFiles fs;
    fs.files["shaders/a.fs"] = "#version 300 es\nvoid main() {}\n";
    const auto result = Preprocess("shaders/a.fs", Stage::Fragment, Dialect::GlslEs300, fs.Reader());
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error.find("preamble"), std::string::npos);
}

TEST(ShaderSource, HandlesCrlfSources) {
    MemoryFiles fs = BaseFiles();
    fs.files["shaders/common/x.glsl"] = "float x() { return 1.0; }\r\n";
    fs.files["shaders/a.fs"] = "#version 300 es\r\n#include \"common/x.glsl\"\r\nvoid main() {}\r\n";
    const auto result = Preprocess("shaders/a.fs", Stage::Fragment, Dialect::GlslEs300, fs.Reader());
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.code.find('\r'), std::string::npos);
}

TEST(ShaderSource, NumberedListingAndSourceLegend) {
    MemoryFiles fs = BaseFiles();
    fs.files["shaders/a.fs"] = "#version 300 es\nvoid main() {}\n";
    const auto result = Preprocess("shaders/a.fs", Stage::Fragment, Dialect::GlslEs300, fs.Reader());
    ASSERT_TRUE(result.ok);
    EXPECT_NE(ShaderSource::DescribeSources(result).find("source 1: shaders/common/preamble.glsl"), std::string::npos);
    EXPECT_EQ(ShaderSource::NumberedListing("a\nb\n"), "1\ta\n2\tb\n");
}

TEST(ShaderSource, StripsSamplerBindingsAndRecordsTheirUnits) {
    MemoryFiles fs = BaseFiles();
    fs.files["shaders/a.fs"] =
        "#version 300 es\n"
        "layout(binding = TU_SHADOW_MAP) uniform sampler2D shadowMap;\n"
        "  layout (binding=3) uniform highp samplerCube sky; // trailing comment\n"
        "layout(location = 0) out vec4 color;\n"
        "void main() {}\n";
    const auto result = Preprocess("shaders/a.fs", Stage::Fragment, Dialect::GlslEs300, fs.Reader());
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.code.find("binding"), std::string::npos);
    EXPECT_NE(result.code.find("\nuniform sampler2D shadowMap;\n"), std::string::npos);
    EXPECT_NE(result.code.find("\n  uniform highp samplerCube sky; // trailing comment\n"), std::string::npos);
    EXPECT_NE(result.code.find("layout(location = 0) out vec4 color;"), std::string::npos); // not a binding
    ASSERT_EQ(result.samplerBindings.size(), 2u);
    EXPECT_EQ(result.samplerBindings[0].uniform, "shadowMap");
    EXPECT_EQ(result.samplerBindings[0].unit, static_cast<int>(TextureUnit::ShadowMap));
    EXPECT_EQ(result.samplerBindings[1].uniform, "sky");
    EXPECT_EQ(result.samplerBindings[1].unit, 3);
}

TEST(ShaderSource, InsertsPermutationDefinesAheadOfThePreamble) {
    MemoryFiles fs = BaseFiles();
    fs.files["shaders/a.fs"] = "#version 300 es\n#ifdef HAS_X\nout vec4 c;\n#endif\n";

    const auto result =
        Preprocess("shaders/a.fs", Stage::Fragment, Dialect::GlslEs300, fs.Reader(), {"HAS_X", "USE_Y2"});
    ASSERT_TRUE(result.ok) << result.error;
    const size_t x = result.code.find("#define HAS_X 1\n");
    const size_t y = result.code.find("#define USE_Y2 1\n");
    const size_t preamble = result.code.find("#line 1 1\n");
    ASSERT_NE(x, std::string::npos);
    ASSERT_NE(y, std::string::npos);
    ASSERT_NE(preamble, std::string::npos);
    EXPECT_LT(x, y);
    EXPECT_LT(y, preamble);
    // The defines do not shift the top file's numbering.
    EXPECT_NE(result.code.find("#line 2 0\n#ifdef HAS_X"), std::string::npos);
}

TEST(ShaderSource, RejectsMalformedOrReservedDefines) {
    MemoryFiles fs = BaseFiles();
    fs.files["shaders/a.fs"] = "#version 300 es\nvoid main() {}\n";
    for (const char* bad : {"", "lower", "1ABC", "HAS-X", "A B", "SS_STAGE_VERTEX", "GL_ES"}) {
        const auto result = Preprocess("shaders/a.fs", Stage::Fragment, Dialect::GlslEs300, fs.Reader(), {bad});
        EXPECT_FALSE(result.ok) << '"' << bad << '"';
        EXPECT_NE(result.error.find("invalid shader define"), std::string::npos) << result.error;
    }
    EXPECT_TRUE(ShaderSource::IsValidDefine("HAS_CLOUDS"));
    EXPECT_TRUE(ShaderSource::IsValidDefine("_X1"));
}

TEST(ShaderSource, ReadsDeclaredPermutations) {
    EXPECT_EQ(ShaderSource::DeclaredPermutations("#version 300 es\n  // permutations: A B\nx\n// permutations: C\n"),
              (std::vector<std::string>{"A", "B", "C"}));
    EXPECT_TRUE(ShaderSource::DeclaredPermutations("// permutation A\nuniform float permutations;\n").empty());
}

TEST(CatalogMaterial, VariantCountsAMapOnlyWhenItIsResident) {
    using namespace CatalogMaterial;
    const Material earth{true, true, true, false, 0.1f};
    EXPECT_EQ(Variant(earth, Textures{1, 2, 3, 4, 5}), kNightTexture | kSpecularMap | kClouds);
    EXPECT_EQ(Variant(earth, Textures{1, 2, 0, 4, 0}), kNightTexture); // specular and clouds still loading
    EXPECT_EQ(Variant(earth, Textures{}), 0u);
    const Material moon{false, false, false, true, 0.1f};
    EXPECT_EQ(Variant(moon, Textures{1, 2, 3, 4, 5}), kSphereIntersect); // flags off: maps ignored
}

TEST(CatalogMaterial, PossibleVariantsAreEverySubsetOfTheRowsMaps) {
    using namespace CatalogMaterial;
    const auto sorted = [](std::vector<unsigned> variants) {
        std::sort(variants.begin(), variants.end());
        return variants;
    };
    EXPECT_EQ(sorted(PossibleVariants({true, true, true, false, 0.1f})),
              (std::vector<unsigned>{0, 1, 2, 3, 4, 5, 6, 7}));
    EXPECT_EQ(sorted(PossibleVariants({false, true, false, true, 0.1f})),
              (std::vector<unsigned>{kSphereIntersect, kSphereIntersect | kSpecularMap}));
    EXPECT_EQ(PossibleVariants({false, false, false, false, 0.1f}), (std::vector<unsigned>{0}));
    EXPECT_EQ(Defines(kSpecularMap | kSphereIntersect),
              (std::vector<std::string>{"HAS_SPECULAR_MAP", "USE_SPHERE_INTERSECT"}));
    EXPECT_TRUE(Defines(0).empty());
}

TEST(ShaderSource, RejectsUnknownUnitsAndBindingsOffSamplers) {
    MemoryFiles fs = BaseFiles();
    fs.files["shaders/unknown.fs"] = "#version 300 es\nlayout(binding = TU_NOPE) uniform sampler2D a;\n";
    fs.files["shaders/block.fs"] = "#version 300 es\nlayout(binding = 0) uniform float notASampler;\n";
    const auto unknown = Preprocess("shaders/unknown.fs", Stage::Fragment, Dialect::GlslEs300, fs.Reader());
    EXPECT_FALSE(unknown.ok);
    EXPECT_NE(unknown.error.find("TU_NOPE"), std::string::npos);
    EXPECT_NE(unknown.error.find("shaders/unknown.fs:2"), std::string::npos);
    const auto block = Preprocess("shaders/block.fs", Stage::Fragment, Dialect::GlslEs300, fs.Reader());
    EXPECT_FALSE(block.ok);
}

// ---------------------------------------------------------------------------------------
// The real shader tree.

#ifdef SOLARSYSTEM_SOURCE_DIR
const std::string kShaderDir = std::string(SOLARSYSTEM_SOURCE_DIR) + "/resource/shaders";

std::vector<std::filesystem::path> TopLevelShaders() {
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(kShaderDir)) {
        const std::string ext = entry.path().extension().string();
        if (ext == ".vs" || ext == ".fs") {
            files.push_back(entry.path());
        }
    }
    return files;
}

TEST(ShaderSourceTree, EveryShaderPreprocessesInBothDialects) {
    const auto files = TopLevelShaders();
    ASSERT_GT(files.size(), 20u);
    for (const auto& file : files) {
        const Stage stage = file.extension() == ".vs" ? Stage::Vertex : Stage::Fragment;
        for (const Dialect dialect : {Dialect::GlslEs300, Dialect::Desktop460}) {
            const auto result = Preprocess(file.string(), stage, dialect, ShaderSource::ReadFileFromDisk);
            ASSERT_TRUE(result.ok) << file << ": " << result.error;
            EXPECT_EQ(result.code.find("#include"), std::string::npos) << file;
        }
    }
}

// The point of common/: a helper lives in exactly one file. Catches a function that gets
// pasted back into a shader that also includes the common copy (a GLSL redefinition error
// the browser would otherwise be first to report).
TEST(ShaderSourceTree, NoFunctionIsDefinedTwiceAfterExpansion) {
    const std::regex definition(R"(^(?:float|vec[234]|bool|void|int|mat[234])\s+(\w+)\s*\(([^)]*)\)\s*\{?\s*$)");
    for (const auto& file : TopLevelShaders()) {
        const Stage stage = file.extension() == ".vs" ? Stage::Vertex : Stage::Fragment;
        const auto result = Preprocess(file.string(), stage, Dialect::GlslEs300, ShaderSource::ReadFileFromDisk);
        ASSERT_TRUE(result.ok) << result.error;
        std::set<std::string> seen;
        std::istringstream lines(result.code);
        std::string line;
        while (std::getline(lines, line)) {
            std::smatch match;
            if (std::regex_match(line, match, definition)) {
                // Overloads are legal: key on name + parameter list.
                const std::string key = match[1].str() + "(" + match[2].str() + ")";
                EXPECT_TRUE(seen.insert(key).second) << file << " defines " << key << " twice";
            }
        }
    }
}

TEST(ShaderSourceTree, PreambleDeclaresNoMacrosThatShadowShaderConstants) {
    const auto preamble = ShaderSource::ReadFileFromDisk(kShaderDir + "/common/preamble.glsl");
    ASSERT_TRUE(preamble.has_value());
    EXPECT_FALSE(std::regex_search(*preamble, std::regex(R"((^|\n)[ \t]*#[ \t]*define)")));
    // The one include it pulls in only defines TU_* names, which no shader constant uses.
    const auto units = ShaderSource::ReadFileFromDisk(kShaderDir + "/common/texture_units.glsl");
    ASSERT_TRUE(units.has_value());
    std::istringstream lines(*units);
    std::string line;
    while (std::getline(lines, line)) {
        if (std::regex_search(line, std::regex(R"(^\s*#\s*define)"))) {
            EXPECT_TRUE(std::regex_search(line, std::regex(R"(^#define TU_\w+ +\d+$)"))) << line;
        }
    }
}

TEST(ShaderSourceTree, TextureUnitsGlslMatchesTheHeader) {
    // scripts/generate-texture-units.mjs writes this file; CI runs it with --check. This is the
    // same check without node: every X-list entry appears as its #define, and nothing else.
    const auto units = ShaderSource::ReadFileFromDisk(kShaderDir + "/common/texture_units.glsl");
    ASSERT_TRUE(units.has_value());
    size_t entries = 0;
#define SOLARSYSTEM_EXPECT_UNIT(name, value, glsl)                                                  \
    ++entries;                                                                                      \
    EXPECT_TRUE(std::regex_search(*units, std::regex("\n#define " #glsl " +" #value "\n"))) << #glsl;
    SOLARSYSTEM_TEXTURE_UNITS(SOLARSYSTEM_EXPECT_UNIT)
#undef SOLARSYSTEM_EXPECT_UNIT
    EXPECT_EQ(CountOf(*units, "#define "), entries);
}

// Every sampler names its unit, so no draw has to SetInt it (and none can forget to).
TEST(ShaderSourceTree, EverySamplerDeclaresItsTextureUnit) {
    const std::regex bareSampler(R"((^|\n)\s*uniform\s+(?:(?:lowp|mediump|highp)\s+)?[iu]?sampler\w*\s+\w+)");
    for (const auto& file : TopLevelShaders()) {
        const Stage stage = file.extension() == ".vs" ? Stage::Vertex : Stage::Fragment;
        const auto raw = ShaderSource::ReadFileFromDisk(file.string());
        ASSERT_TRUE(raw.has_value());
        EXPECT_FALSE(std::regex_search(*raw, bareSampler)) << file << " has a sampler without layout(binding = TU_...)";
        const auto result = Preprocess(file.string(), stage, Dialect::GlslEs300, ShaderSource::ReadFileFromDisk);
        ASSERT_TRUE(result.ok) << result.error;
        EXPECT_FALSE(std::regex_search(result.code, std::regex(R"((^|\n)\s*layout\s*\(\s*binding)")))
            << file << ": GLSL ES 3.00 has no sampler binding";
    }
}

// The lit programs (vertex + fragment pairs Renderer builds) read the per-frame uniforms
// from FrameUBO rather than taking projection/view/zCoef/lightPos per draw.
TEST(ShaderSourceTree, LitProgramsReadTheFrameUniformBlock) {
    const std::pair<const char*, const char*> programs[] = {
        {"planetLighting.vs", "planetLighting.fs"}, {"planetLighting.vs", "cloudsLighting.fs"},
        {"atmosphere.vs", "atmosphere.fs"},         {"atmosphere.vs", "atmospherePbr.fs"},
        {"planetaryRingLighting.vs", "planetaryRingLighting.fs"},
        {"star.vs", "star.fs"},                     {"starCorona.vs", "starCorona.fs"},
        {"starCoronaVolume.vs", "starCoronaVolume.fs"},
    };
    for (const auto& [vs, fs] : programs) {
        const auto vertex = Preprocess(kShaderDir + "/" + vs, Stage::Vertex, Dialect::GlslEs300, ShaderSource::ReadFileFromDisk);
        ASSERT_TRUE(vertex.ok) << vertex.error;
        EXPECT_NE(vertex.code.find("uniform FrameUBO"), std::string::npos) << vs;
        const auto fragment = Preprocess(kShaderDir + "/" + fs, Stage::Fragment, Dialect::GlslEs300, ShaderSource::ReadFileFromDisk);
        ASSERT_TRUE(fragment.ok) << fragment.error;
        for (const char* stale : {"uniform mat4 projection;", "uniform mat4 view;", "uniform float zCoef;"}) {
            EXPECT_EQ(vertex.code.find(stale), std::string::npos) << vs << " still declares " << stale;
            EXPECT_EQ(fragment.code.find(stale), std::string::npos) << fs << " still declares " << stale;
        }
    }
}

// CatalogMaterial::Defines() and planetLighting.fs's `// permutations:` line name the same
// switches (the self-test compiles every subset of that line), and the old flag uniforms
// are gone: a flag is a permutation now, not a per-draw bool.
TEST(ShaderSourceTree, PlanetLightingDeclaresExactlyTheMaterialDefines) {
    const auto text = ShaderSource::ReadFileFromDisk(kShaderDir + "/planetLighting.fs");
    ASSERT_TRUE(text.has_value());
    EXPECT_EQ(ShaderSource::DeclaredPermutations(*text), CatalogMaterial::Defines(CatalogMaterial::kVariantCount - 1));
    for (const char* stale : {"uniform bool hasNightTexture", "uniform bool hasSpecularMap", "uniform bool hasSpecular;",
                              "uniform bool hasClouds", "uniform bool isUseSphereIntersect"}) {
        EXPECT_EQ(text->find(stale), std::string::npos) << "planetLighting.fs still declares " << stale;
    }
}

// Every declared switch is a valid define that the stage actually tests, and every subset
// preprocesses (the GL self-test then compiles them).
TEST(ShaderSourceTree, EveryDeclaredPermutationIsUsedAndPreprocesses) {
    size_t declaring = 0;
    for (const auto& file : TopLevelShaders()) {
        const auto text = ShaderSource::ReadFileFromDisk(file.string());
        ASSERT_TRUE(text.has_value()) << file;
        const std::vector<std::string> permutations = ShaderSource::DeclaredPermutations(*text);
        if (permutations.empty()) {
            continue;
        }
        ++declaring;
        for (const std::string& define : permutations) {
            EXPECT_TRUE(ShaderSource::IsValidDefine(define)) << file << ": " << define;
            EXPECT_GE(CountOf(*text, define), 2u) << file << " declares " << define << " but never tests it";
        }
        const Stage stage = file.extension() == ".vs" ? Stage::Vertex : Stage::Fragment;
        for (unsigned mask = 0; mask < (1u << permutations.size()); ++mask) {
            std::vector<std::string> defines;
            for (size_t bit = 0; bit < permutations.size(); ++bit) {
                if (mask & (1u << bit)) {
                    defines.push_back(permutations[bit]);
                }
            }
            const auto result =
                Preprocess(file.string(), stage, Dialect::GlslEs300, ShaderSource::ReadFileFromDisk, defines);
            EXPECT_TRUE(result.ok) << file << " mask " << mask << ": " << result.error;
        }
    }
    EXPECT_GE(declaring, 1u);
}
#endif

} // namespace
