#include "ShaderSource.h"

#include <cctype>
#include <fstream>
#include <sstream>
#include <unordered_set>

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
    const FileReader& reader;
    std::string root; // directory of the top-level file; includes resolve against it
    std::vector<std::string> names;
    std::unordered_set<std::string> included;
    std::vector<std::string> stack;
    std::string code;
    std::string error;
};

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
            ctx.code += line;
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

Result Preprocess(const std::string& path, Stage stage, Dialect dialect, const FileReader& reader) {
    Result result;
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

    Context ctx{reader, DirectoryOf(top), {}, {}, {}, {}, {}};
    ctx.names.push_back(top);
    ctx.included.insert(top);
    ctx.stack.push_back(top);

    // GLSL ES 3.00 is a subset of desktop GLSL 4.60 (precision qualifiers included), so
    // the native build only needs a different directive; it then no longer depends on
    // ARB_ES3_compatibility. Line 1 stays line 1, so on-disk numbering is preserved.
    const bool rewrite = dialect == Dialect::Desktop460 && lines[0].find("es") != std::string::npos;
    ctx.code += rewrite ? kDesktopVersionDirective : lines[0];
    ctx.code += '\n';
    switch (stage) {
        case Stage::Vertex: ctx.code += "#define SS_STAGE_VERTEX 1\n"; break;
        case Stage::Fragment: ctx.code += "#define SS_STAGE_FRAGMENT 1\n"; break;
        case Stage::Geometry: ctx.code += "#define SS_STAGE_GEOMETRY 1\n"; break;
    }
    ctx.code += dialect == Dialect::GlslEs300 ? "#define SS_GLSL_ES 1\n" : "#define SS_GLSL_DESKTOP 1\n";

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

    result.ok = true;
    result.code = std::move(ctx.code);
    result.sourceNames = std::move(ctx.names);
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
