#include "Shader.h"
#include "../gfx/FrameUniforms.h"
#include <algorithm>
#include <cstring>

#ifndef __EMSCRIPTEN__
#include <filesystem>
#endif

namespace {

// Shader sources are authored once as GLSL ES 3.00 (the only dialect WebGL 2 accepts) and
// assembled by ShaderSource: the native build rewrites `#version 300 es` to
// `#version 460 core`, the shared common/preamble.glsl is inserted and
// `#include "common/…"` lines are expanded. See docs/ARCHITECTURE.md §9.1.
ShaderSource::Result AssembleShader(const std::string& path, ShaderSource::Stage stage,
                                    ShaderSource::Dialect dialect = ShaderSource::NativeDialect()) {
    ShaderSource::Result result = ShaderSource::Preprocess(path, stage, dialect, ShaderSource::ReadFileFromDisk);
    if (!result.ok) {
        std::cerr << "ERROR::SHADER::PREPROCESS_FAILED: " << result.error << std::endl;
        throw std::runtime_error("ERROR::SHADER::PREPROCESS_FAILED: " + result.error);
    }
    return result;
}

} // namespace

Shader::Shader(const std::string& vertexPath, const std::string& fragmentPath) {
    Build(vertexPath, fragmentPath, "");
}

#ifndef __EMSCRIPTEN__
Shader::Shader(const std::string& vertexPath, const std::string& fragmentPath, const std::string& geometryPath) {
    Build(vertexPath, fragmentPath, geometryPath);
}
#endif

void Shader::Build(const std::string& vertexPath, const std::string& fragmentPath, const std::string& geometryPath) {
#ifdef __EMSCRIPTEN__
    // Defence in depth behind the missing 3-arg constructor: nothing in the web build can
    // reach a geometry stage, because WebGL 2 does not have one.
    (void)geometryPath;
#endif

    const ShaderSource::Result vertexSource = AssembleShader(vertexPath, ShaderSource::Stage::Vertex);
    const ShaderSource::Result fragmentSource = AssembleShader(fragmentPath, ShaderSource::Stage::Fragment);

    const char* vShaderCode = vertexSource.code.c_str();
    const char* fShaderCode = fragmentSource.code.c_str();

    // Stages are owned handles, so a CheckCompileErrors throw no longer leaks them.
    const GlShaderStage vertex = GlShaderStage::Create(GL_VERTEX_SHADER);
    glShaderSource(vertex.get(), 1, &vShaderCode, nullptr);
    glCompileShader(vertex.get());
    CheckCompileErrors(vertex.get(), ShaderType::VertexShader, vertexPath, &vertexSource);

    const GlShaderStage fragment = GlShaderStage::Create(GL_FRAGMENT_SHADER);
    glShaderSource(fragment.get(), 1, &fShaderCode, nullptr);
    glCompileShader(fragment.get());
    CheckCompileErrors(fragment.get(), ShaderType::FragmentShader, fragmentPath, &fragmentSource);

#ifndef __EMSCRIPTEN__
    GlShaderStage geometry;
    const bool hasGeometry = !geometryPath.empty();
    if (hasGeometry) {
        const ShaderSource::Result geometrySource = AssembleShader(geometryPath, ShaderSource::Stage::Geometry);
        const char* gShaderCode = geometrySource.code.c_str();
        geometry = GlShaderStage::Create(GL_GEOMETRY_SHADER);
        glShaderSource(geometry.get(), 1, &gShaderCode, nullptr);
        glCompileShader(geometry.get());
        CheckCompileErrors(geometry.get(), ShaderType::GeometryShader, geometryPath, &geometrySource);
    }
#endif

    _program = GlProgram::Create();
    const GLuint program = _program.get();
    glAttachShader(program, vertex.get());
    glAttachShader(program, fragment.get());
#ifndef __EMSCRIPTEN__
    if (hasGeometry) {
        glAttachShader(program, geometry.get());
    }
#endif
    glLinkProgram(program);
    CheckCompileErrors(program, ShaderType::ShaderProgram, fragmentPath);

    glDetachShader(program, vertex.get());
    glDetachShader(program, fragment.get());
#ifndef __EMSCRIPTEN__
    if (hasGeometry) {
        glDetachShader(program, geometry.get());
    }
#endif

    std::vector<ShaderSource::SamplerBinding> samplerBindings = vertexSource.samplerBindings;
    samplerBindings.insert(samplerBindings.end(), fragmentSource.samplerBindings.begin(),
                           fragmentSource.samplerBindings.end());
    Introspect(samplerBindings);
}

void Shader::Introspect(const std::vector<ShaderSource::SamplerBinding>& samplerBindings) {
    const GLuint program = _program.get();
    GLint count = 0;
    GLint maxLength = 0;
    glGetProgramiv(program, GL_ACTIVE_UNIFORMS, &count);
    glGetProgramiv(program, GL_ACTIVE_UNIFORM_MAX_LENGTH, &maxLength);
    std::vector<char> nameBuffer(static_cast<size_t>(std::max(maxLength, 1)));
    _slots.clear();
    _uniformIndex.clear();
    for (GLint i = 0; i < count; ++i) {
        GLsizei length = 0;
        GLint size = 0;
        GLenum type = 0;
        glGetActiveUniform(program, static_cast<GLuint>(i), static_cast<GLsizei>(nameBuffer.size()), &length, &size,
                           &type, nameBuffer.data());
        std::string name(nameBuffer.data(), static_cast<size_t>(length));
        const GLint location = glGetUniformLocation(program, name.c_str());
        if (location < 0) {
            continue; // a uniform-block member
        }
        const auto slot = static_cast<unsigned int>(_slots.size());
        _slots.push_back(UniformSlot{location});
        if (name.size() > 3 && name.compare(name.size() - 3, 3, "[0]") == 0) {
            _uniformIndex.emplace_back(name.substr(0, name.size() - 3), slot);
        }
        _uniformIndex.emplace_back(std::move(name), slot);
    }
    std::sort(_uniformIndex.begin(), _uniformIndex.end());

    // SetInt binds the program for the call and restores the current one.
    for (const ShaderSource::SamplerBinding& binding : samplerBindings) {
        SetInt(binding.uniform, binding.unit);
    }

    const GLuint frameBlock = glGetUniformBlockIndex(program, "FrameUBO");
    _usesFrameUniforms = frameBlock != GL_INVALID_INDEX;
    if (_usesFrameUniforms) {
        glUniformBlockBinding(program, frameBlock, FrameUniformBuffer::kBinding);
    }
}

Shader::UniformSlot* Shader::FindSlot(std::string_view name) const {
    const auto it = std::lower_bound(_uniformIndex.begin(), _uniformIndex.end(), name,
                                     [](const auto& entry, std::string_view key) { return entry.first < key; });
    return it != _uniformIndex.end() && it->first == name ? &_slots[it->second] : nullptr;
}

template <class T>
bool Shader::Store(UniformSlot& slot, const T& value) {
    static_assert(sizeof(T) <= sizeof(UniformSlot::value), "uniform value larger than a mat4");
    if (slot.size == sizeof(T) && std::memcmp(slot.value, &value, sizeof(T)) == 0) {
        return false;
    }
    std::memcpy(slot.value, &value, sizeof(T));
    slot.size = static_cast<unsigned char>(sizeof(T));
    return true;
}

void Shader::Use() const {
    glUseProgram(_program.get());
    s_currentProgram = _program.get();
}

// Every setter: find the slot, skip an unchanged value, and only then count and issue the
// call. glUniform* writes to the *current* program, so a setter on a Shader that is not the
// one last Use()d binds its own program for the call and puts the current one back; the
// value then lands where the caller meant it, and the slot cache stays true.
#define SOLARSYSTEM_SHADER_SET(stored, call)                          \
    UniformSlot* slot = FindSlot(name);                               \
    if (slot && Store(*slot, stored)) {                               \
        const GLint location = slot->location;                        \
        const GLuint current = s_currentProgram;                      \
        const bool foreign = current != _program.get();               \
        if (foreign) {                                                \
            glUseProgram(_program.get());                             \
        }                                                             \
        ++s_uniformCalls;                                             \
        call;                                                         \
        if (foreign) {                                                \
            glUseProgram(current);                                    \
        }                                                             \
    }

void Shader::SetBool(std::string_view name, bool value) const {
    // Stored as the int GL keeps, so SetBool(true) and SetInt(1) agree.
    const int asInt = value ? 1 : 0;
    SOLARSYSTEM_SHADER_SET(asInt, glUniform1i(location, asInt))
}

void Shader::SetInt(std::string_view name, int value) const {
    SOLARSYSTEM_SHADER_SET(value, glUniform1i(location, value))
}

void Shader::SetFloat(std::string_view name, float value) const {
    SOLARSYSTEM_SHADER_SET(value, glUniform1f(location, value))
}

void Shader::SetVec2(std::string_view name, const glm::vec2& value) const {
    SOLARSYSTEM_SHADER_SET(value, glUniform2fv(location, 1, &value[0]))
}

void Shader::SetVec2(std::string_view name, float x, float y) const {
    SetVec2(name, glm::vec2(x, y));
}

void Shader::SetVec3(std::string_view name, const glm::vec3& value) const {
    SOLARSYSTEM_SHADER_SET(value, glUniform3fv(location, 1, &value[0]))
}

void Shader::SetVec3(std::string_view name, float x, float y, float z) const {
    SetVec3(name, glm::vec3(x, y, z));
}

void Shader::SetVec4(std::string_view name, const glm::vec4& value) const {
    SOLARSYSTEM_SHADER_SET(value, glUniform4fv(location, 1, &value[0]))
}

void Shader::SetMat3(std::string_view name, const glm::mat3& mat) const {
    SOLARSYSTEM_SHADER_SET(mat, glUniformMatrix3fv(location, 1, GL_FALSE, &mat[0][0]))
}

void Shader::SetMat4(std::string_view name, const glm::mat4& mat) const {
    SOLARSYSTEM_SHADER_SET(mat, glUniformMatrix4fv(location, 1, GL_FALSE, &mat[0][0]))
}

#undef SOLARSYSTEM_SHADER_SET

#ifndef __EMSCRIPTEN__
int Shader::SelfTestDirectory(const std::string& directory) {
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        const std::string extension = entry.path().extension().string();
        if (entry.is_regular_file() && (extension == ".vs" || extension == ".fs")) {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());

    // Both dialects: the native one, and GLSL ES 3.00 exactly as the web build assembles it.
    // Desktop drivers that expose ARB_ES3_compatibility (Mesa does) compile `#version 300 es`
    // too, so CI catches an ES-only error without a browser.
    const ShaderSource::Dialect dialects[] = {ShaderSource::Dialect::Desktop460, ShaderSource::Dialect::GlslEs300};
    int failures = 0;
    size_t compiled = 0;
    for (const ShaderSource::Dialect dialect : dialects) {
        const char* dialectName = dialect == ShaderSource::Dialect::Desktop460 ? "460 core" : "300 es";
        for (const auto& file : files) {
            const bool vertex = file.extension() == ".vs";
            const std::string path = directory + "/" + file.filename().string();
            try {
                const ShaderSource::Result source =
                    AssembleShader(path, vertex ? ShaderSource::Stage::Vertex : ShaderSource::Stage::Fragment, dialect);
                const char* code = source.code.c_str();
                const GlShaderStage shader = GlShaderStage::Create(vertex ? GL_VERTEX_SHADER : GL_FRAGMENT_SHADER);
                glShaderSource(shader.get(), 1, &code, nullptr);
                glCompileShader(shader.get());
                CheckCompileErrors(shader.get(), vertex ? ShaderType::VertexShader : ShaderType::FragmentShader,
                                   path + " (" + dialectName + ")", &source);
                ++compiled;
            } catch (const std::exception&) {
                ++failures; // already printed by AssembleShader / CheckCompileErrors
            }
        }
    }
    std::cout << "[ShaderSelfTest] " << compiled << "/" << (compiled + static_cast<size_t>(failures))
              << " shader stages compiled (460 core + 300 es)" << (failures == 0 ? " OK" : " - FAILURES") << std::endl;
    return failures;
}
#endif

void Shader::CheckCompileErrors(size_t shader, ShaderType type, const std::string& path,
                                const ShaderSource::Result* source) {
    int success;
    char infoLog[1024];
    if (type != ShaderType::ShaderProgram) {
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(shader, 1024, nullptr, infoLog);
            std::cerr << "ERROR::SHADER::COMPILATION_FAILED\nType: " << ShaderTypeToString(type)
                      << "\nPath: " << path << "\n" << infoLog;
            if (source) {
                // Errors are reported as <source string>:<line>; map the numbers back to
                // files, and dump the assembled text in case a driver numbers #line oddly.
                std::cerr << "Source strings:\n" << ShaderSource::DescribeSources(*source)
                          << "Assembled source:\n" << ShaderSource::NumberedListing(source->code);
            }
            std::cerr << "\n -- --------------------------------------------------- -- " << std::endl;
            throw std::runtime_error("ERROR::SHADER_COMPILATION_ERROR of type: " + ShaderTypeToString(type) + " " + path + "\n" + std::string(infoLog) + "\n -- --------------------------------------------------- -- ");
        }
    }
    else {
        glGetProgramiv(shader, GL_LINK_STATUS, &success);
        if (!success) {
            glGetProgramInfoLog(shader, 1024, nullptr, infoLog);
            std::cerr << "ERROR::SHADER::PROGRAM_LINKING_FAILED\nType: " << ShaderTypeToString(type)
                      << "\nPath: " << path << "\n" << infoLog << "\n -- --------------------------------------------------- -- " << std::endl;
            throw std::runtime_error("ERROR::PROGRAM_LINKING_ERROR of type: " + ShaderTypeToString(type) + "\n" + std::string(infoLog) + path + "\n -- --------------------------------------------------- --");
        }
    }
}

std::string Shader::ShaderTypeToString(ShaderType type) {
    switch(type) {
        case ShaderType::VertexShader: return "Vertex Shader";
        case ShaderType::FragmentShader: return "Fragment Shader";
        case ShaderType::GeometryShader: return "Geometry Shader";
        case ShaderType::ShaderProgram: return "Shader Program";
        default: return "";
    }
}
