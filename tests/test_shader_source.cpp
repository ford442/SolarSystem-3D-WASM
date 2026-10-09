// Covers ShaderSource: the #version rewrite, the preamble, #include expansion and the
// #line bookkeeping that keeps compiler errors pointing at the right file and line. The
// last tests run every real shader in resource/shaders through the preprocessor.

#include "Auxiliary_Modules/ShaderSource.h"

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
}
#endif

} // namespace
