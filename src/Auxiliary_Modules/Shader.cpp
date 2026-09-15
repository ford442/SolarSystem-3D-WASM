#include "Shader.h"

namespace {

// The shader sources in resource/shaders/ are authored once, in GLSL ES 3.00, because
// that is the only dialect WebGL 2 accepts. The native build runs an OpenGL 4.6 core
// context, where `#version 300 es` is only accepted via ARB_ES3_compatibility — widely
// implemented, but not guaranteed. Rewriting the version directive to the matching
// desktop dialect (GLSL ES 3.00 is a subset of desktop GLSL 4.60, precision qualifiers
// included) removes that dependency. The replacement keeps the directive on its own
// first line so compiler error line numbers still match the file on disk.
// See docs/ARCHITECTURE.md §9.1.
#ifndef __EMSCRIPTEN__
constexpr const char* kNativeVersionDirective = "#version 460 core";

void PatchVersionDirective(std::string& source) {
    const size_t directive = source.find("#version");
    if (directive == std::string::npos) {
        return; // Empty/unreadable file; the compile error below will report it.
    }
    size_t lineEnd = source.find('\n', directive);
    if (lineEnd == std::string::npos) {
        lineEnd = source.size();
    }
    const std::string original = source.substr(directive, lineEnd - directive);
    if (original.find("es") == std::string::npos) {
        return; // Already a desktop directive; leave it alone.
    }
    source.replace(directive, lineEnd - directive, kNativeVersionDirective);
}
#endif

std::string ReadShaderFile(const std::string& path) {
    std::ifstream file;
    file.exceptions(std::ifstream::failbit | std::ifstream::badbit);
    std::string source;
    try {
        file.open(path);
        std::ostringstream stream;
        stream << file.rdbuf();
        file.close();
        source = stream.str();
    } catch (const std::ifstream::failure&) {
        std::cerr << "ERROR::SHADER::FILE_NOT_SUCCESFULLY_READ: " << path << std::endl;
        return source;
    }
#ifndef __EMSCRIPTEN__
    PatchVersionDirective(source);
#endif
    return source;
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
    _shaderProgramID = 0;

#ifdef __EMSCRIPTEN__
    // Defence in depth behind the missing 3-arg constructor: nothing in the web build can
    // reach a geometry stage, because WebGL 2 does not have one.
    (void)geometryPath;
#endif

    const std::string vertexCode = ReadShaderFile(vertexPath);
    const std::string fragmentCode = ReadShaderFile(fragmentPath);

    const char* vShaderCode = vertexCode.c_str();
    const char* fShaderCode = fragmentCode.c_str();

    const GLuint vertex = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex, 1, &vShaderCode, nullptr);
    glCompileShader(vertex);
    CheckCompileErrors(vertex, ShaderType::VertexShader, vertexPath);

    const GLuint fragment = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment, 1, &fShaderCode, nullptr);
    glCompileShader(fragment);
    CheckCompileErrors(fragment, ShaderType::FragmentShader, fragmentPath);

#ifndef __EMSCRIPTEN__
    GLuint geometry = 0;
    const bool hasGeometry = !geometryPath.empty();
    std::string geometryCode;
    if (hasGeometry) {
        geometryCode = ReadShaderFile(geometryPath);
        const char* gShaderCode = geometryCode.c_str();
        geometry = glCreateShader(GL_GEOMETRY_SHADER);
        glShaderSource(geometry, 1, &gShaderCode, nullptr);
        glCompileShader(geometry);
        CheckCompileErrors(geometry, ShaderType::GeometryShader, geometryPath);
    }
#endif

    _shaderProgramID = glCreateProgram();
    glAttachShader(_shaderProgramID, vertex);
    glAttachShader(_shaderProgramID, fragment);
#ifndef __EMSCRIPTEN__
    if (hasGeometry) {
        glAttachShader(_shaderProgramID, geometry);
    }
#endif
    glLinkProgram(_shaderProgramID);
    CheckCompileErrors(_shaderProgramID, ShaderType::ShaderProgram, fragmentPath);

    glDetachShader(_shaderProgramID, vertex);
    glDeleteShader(vertex);
    glDetachShader(_shaderProgramID, fragment);
    glDeleteShader(fragment);
#ifndef __EMSCRIPTEN__
    if (hasGeometry) {
        glDetachShader(_shaderProgramID, geometry);
        glDeleteShader(geometry);
    }
#endif
}

Shader::~Shader() {
    Release();
}

Shader::Shader(Shader&& other) noexcept
    : _shaderProgramID(other._shaderProgramID)
    , _uniformLocationCache(std::move(other._uniformLocationCache)) {
    other._shaderProgramID = 0;
}

Shader& Shader::operator=(Shader&& other) noexcept {
    if (this != &other) {
        Release();
        _shaderProgramID = other._shaderProgramID;
        _uniformLocationCache = std::move(other._uniformLocationCache);
        other._shaderProgramID = 0;
    }
    return *this;
}

void Shader::Release() {
    if (_shaderProgramID != 0) {
        glDeleteProgram(static_cast<GLuint>(_shaderProgramID));
        _shaderProgramID = 0;
    }
    _uniformLocationCache.clear();
}

GLint Shader::GetUniformLocation(const std::string& name) const {
    const auto it = _uniformLocationCache.find(name);
    if (it != _uniformLocationCache.end()) {
        return it->second;
    }
    const GLint location = glGetUniformLocation(static_cast<GLuint>(_shaderProgramID), name.c_str());
    _uniformLocationCache.emplace(name, location);
    return location;
}

void Shader::Use() const {
    glUseProgram(_shaderProgramID);
}

void Shader::SetBool(const std::string& name, bool value) const {
    const GLint location = GetUniformLocation(name);
    if (location >= 0) {
        glUniform1i(location, static_cast<int>(value));
    }
}

void Shader::SetInt(const std::string& name, int value) const {
    const GLint location = GetUniformLocation(name);
    if (location >= 0) {
        glUniform1i(location, value);
    }
}

void Shader::SetFloat(const std::string& name, float value) const {
    const GLint location = GetUniformLocation(name);
    if (location >= 0) {
        glUniform1f(location, value);
    }
}

void Shader::SetDouble(const std::string &name, double value) const {
    // [PORTING NOTE] WebGL 2 does NOT support glUniform1d (double precision).
    // Cast to float for web builds.
    const GLint location = GetUniformLocation(name);
    if (location < 0) {
        return;
    }
    #ifdef __EMSCRIPTEN__
    glUniform1f(location, static_cast<float>(value));
    #else
    glUniform1d(location, value);
    #endif
}

void Shader::SetVec2(const std::string& name, const glm::vec2& value) const {
    const GLint location = GetUniformLocation(name);
    if (location >= 0) {
        glUniform2fv(location, 1, &value[0]);
    }
}

void Shader::SetVec2(const std::string& name, float x, float y) const {
    const GLint location = GetUniformLocation(name);
    if (location >= 0) {
        glUniform2f(location, x, y);
    }
}

void Shader::SetVec3(const std::string& name, const glm::vec3& value) const {
    const GLint location = GetUniformLocation(name);
    if (location >= 0) {
        glUniform3fv(location, 1, &value[0]);
    }
}

void Shader::SetVec3(const std::string& name, float x, float y, float z) const {
    const GLint location = GetUniformLocation(name);
    if (location >= 0) {
        glUniform3f(location, x, y, z);
    }
}

void Shader::SetVec4(const std::string& name, const glm::vec4& value) const {
    const GLint location = GetUniformLocation(name);
    if (location >= 0) {
        glUniform4fv(location, 1, &value[0]);
    }
}

void Shader::SetVec4(const std::string& name, float x, float y, float z, float w) const {
    const GLint location = GetUniformLocation(name);
    if (location >= 0) {
        glUniform4f(location, x, y, z, w);
    }
}

void Shader::SetMat2(const std::string& name, const glm::mat2& mat) const {
    const GLint location = GetUniformLocation(name);
    if (location >= 0) {
        glUniformMatrix2fv(location, 1, GL_FALSE, &mat[0][0]);
    }
}

void Shader::SetMat3(const std::string& name, const glm::mat3& mat) const {
    const GLint location = GetUniformLocation(name);
    if (location >= 0) {
        glUniformMatrix3fv(location, 1, GL_FALSE, &mat[0][0]);
    }
}

void Shader::SetMat4(const std::string& name, const glm::mat4& mat) const {
    const GLint location = GetUniformLocation(name);
    if (location >= 0) {
        glUniformMatrix4fv(location, 1, GL_FALSE, &mat[0][0]);
    }
}

void Shader::SetVec2Double(const std::string& name, const glm::dvec2& value) const {
    const GLint location = GetUniformLocation(name);
    if (location < 0) {
        return;
    }
    #ifdef __EMSCRIPTEN__
    glUniform2fv(location, 1, &glm::vec2(value)[0]);
    #else
    glUniform2dv(location, 1, &value[0]);
    #endif
}

void Shader::SetVec2Double(const std::string& name, double x, double y) const {
    const GLint location = GetUniformLocation(name);
    if (location < 0) {
        return;
    }
    #ifdef __EMSCRIPTEN__
    glUniform2f(location, (float)x, (float)y);
    #else
    glUniform2d(location, x, y);
    #endif
}

void Shader::SetVec3Double(const std::string& name, const glm::dvec3& value) const {
    const GLint location = GetUniformLocation(name);
    if (location < 0) {
        return;
    }
    #ifdef __EMSCRIPTEN__
    glUniform3fv(location, 1, &glm::vec3(value)[0]);
    #else
    glUniform3dv(location, 1, &value[0]);
    #endif
}

void Shader::SetVec3Double(const std::string& name, double x, double y, double z) const {
    const GLint location = GetUniformLocation(name);
    if (location < 0) {
        return;
    }
    #ifdef __EMSCRIPTEN__
    glUniform3f(location, (float)x, (float)y, (float)z);
    #else
    glUniform3d(location, x, y, z);
    #endif
}

void Shader::SetVec4Double(const std::string& name, const glm::dvec4& value) const {
    const GLint location = GetUniformLocation(name);
    if (location < 0) {
        return;
    }
    #ifdef __EMSCRIPTEN__
    glUniform4fv(location, 1, &glm::vec4(value)[0]);
    #else
    glUniform4dv(location, 1, &value[0]);
    #endif
}

void Shader::SetVec4Double(const std::string& name, double x, double y, double z, double w) const {
    const GLint location = GetUniformLocation(name);
    if (location < 0) {
        return;
    }
    #ifdef __EMSCRIPTEN__
    glUniform4f(location, (float)x, (float)y, (float)z, (float)w);
    #else
    glUniform4d(location, x, y, z, w);
    #endif
}

void Shader::SetMat2Double(const std::string& name, const glm::dmat2& mat) const {
    const GLint location = GetUniformLocation(name);
    if (location < 0) {
        return;
    }
    #ifdef __EMSCRIPTEN__
    glUniformMatrix2fv(location, 1, GL_FALSE, &glm::mat2(mat)[0][0]);
    #else
    glUniformMatrix2dv(location, 1, GL_FALSE, &mat[0][0]);
    #endif
}

void Shader::SetMat3Double(const std::string& name, const glm::dmat3& mat) const {
    const GLint location = GetUniformLocation(name);
    if (location < 0) {
        return;
    }
    #ifdef __EMSCRIPTEN__
    glUniformMatrix3fv(location, 1, GL_FALSE, &glm::mat3(mat)[0][0]);
    #else
    glUniformMatrix3dv(location, 1, GL_FALSE, &mat[0][0]);
    #endif
}

void Shader::SetMat4Double(const std::string& name, const glm::dmat4& mat) const {
    const GLint location = GetUniformLocation(name);
    if (location < 0) {
        return;
    }
    #ifdef __EMSCRIPTEN__
    glUniformMatrix4fv(location, 1, GL_FALSE, &glm::mat4(mat)[0][0]);
    #else
    glUniformMatrix4dv(location, 1, GL_FALSE, &mat[0][0]);
    #endif
}

size_t Shader::GetProgramId() const {
    return _shaderProgramID;
}

void Shader::CheckCompileErrors(size_t shader, ShaderType type, const std::string& path) {
    int success;
    char infoLog[1024];
    if (type != ShaderType::ShaderProgram) {
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(shader, 1024, nullptr, infoLog);
            std::cerr << "ERROR::SHADER::COMPILATION_FAILED\nType: " << ShaderTypeToString(type)
                      << "\nPath: " << path << "\n" << infoLog << "\n -- --------------------------------------------------- -- " << std::endl;
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
