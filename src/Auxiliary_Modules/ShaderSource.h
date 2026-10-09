#ifndef SOLARSYSTEM_SHADER_SOURCE_H
#define SOLARSYSTEM_SHADER_SOURCE_H

#include <functional>
#include <optional>
#include <string>
#include <vector>

// GL-free shader source assembly: the `#version` rewrite, the shared preamble and
// `#include "common/x.glsl"` expansion. Shader.cpp feeds the result to glShaderSource;
// tests feed it in-memory files. See docs/ARCHITECTURE.md §9.1.
//
// Output layout (source-string numbers are what the GLSL compiler prints in errors):
//   #version …                   <- top file line 1 (rewritten to 460 core on native)
//   #define SS_STAGE_* / SS_GLSL_*
//   #line 1 1                    <- source string 1 = common/preamble.glsl
//   …preamble…
//   #line 2 0                    <- source string 0 = the top file, on-disk numbering
//   …top file; each include becomes `#line 1 <id>` … `#line <n+1> <parent id>`
//
// `#line L` means "the next line is L" in both GLSL ES 3.00 and desktop GLSL 3.30+, the
// only two dialects this project compiles. Only integer source strings are portable.
namespace ShaderSource {

enum class Stage { Vertex, Fragment, Geometry };
enum class Dialect { GlslEs300, Desktop460 };

// Returns the file's contents, or nullopt when it cannot be read.
using FileReader = std::function<std::optional<std::string>(const std::string& path)>;

struct Result {
    bool ok = false;
    std::string code;
    // sourceNames[i] is the path that GLSL source-string number i refers to.
    std::vector<std::string> sourceNames;
    std::string error;
};

constexpr const char* kPreambleRelativePath = "common/preamble.glsl";
constexpr int kMaxIncludeDepth = 16;

// The dialect this build compiles shaders for.
Dialect NativeDialect();

// Expands `path` (a top-level .vs/.fs file). Includes resolve relative to the top file's
// directory; each file is expanded at most once per stage (no include guards needed).
Result Preprocess(const std::string& path, Stage stage, Dialect dialect, const FileReader& reader);

// Reads from the (MEM)FS; what Shader.cpp uses.
std::optional<std::string> ReadFileFromDisk(const std::string& path);

// "0: resource/shaders/x.fs, 1: …/preamble.glsl, …" for compile-error reports.
std::string DescribeSources(const Result& result);

// The expanded source with line numbers, printed when a compile fails so the location is
// readable even if a driver numbers `#line` differently.
std::string NumberedListing(const std::string& code);

} // namespace ShaderSource

#endif // SOLARSYSTEM_SHADER_SOURCE_H
