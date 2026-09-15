#ifndef SOLARSYSTEM_SHADER_H
#define SOLARSYSTEM_SHADER_H
#include <GL/glew.h>
#ifdef __EMSCRIPTEN__
// Emscripten's glew/SDL_opengl declare DSA entry points that WebGL 2 does not
// implement. Macro (not inline) so -flto cannot emit an unresolved import.
//
// NOTE: this polyfill is GL_TEXTURE_2D ONLY, while the native glBindTextureUnit it
// stands in for is target-agnostic. A cube map (or any other target) bound through it
// works natively and silently binds nothing on web — see SkyBox::Render, which guards
// the call with #ifndef __EMSCRIPTEN__ and binds GL_TEXTURE_CUBE_MAP by hand. Treat this
// as `BindTexture2D` at every call site. Mirrored in src/SystemModules.h.
#undef glBindTextureUnit
#define glBindTextureUnit(unit, texture) \
    do { \
        glActiveTexture(GL_TEXTURE0 + (unit)); \
        glBindTexture(GL_TEXTURE_2D, (texture)); \
    } while (0)
#endif
#include <glm/glm.hpp>
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <unordered_map>

class Shader {
public:
    Shader(const std::string& vertexPath, const std::string& fragmentPath);
#ifndef __EMSCRIPTEN__
    // Geometry shaders are desktop-only: WebGL 2 / GLES 3.0 have no geometry stage, so
    // this overload does not exist in the Emscripten build and passing a geometry path
    // there is a compile error rather than a shader that silently fails to link.
    Shader(const std::string& vertexPath, const std::string& fragmentPath, const std::string& geometryPath);
#endif
    ~Shader();
    
    // Delete copy to prevent double-free
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;
    
    // Move semantics for proper ownership transfer
    Shader(Shader&& other) noexcept;
    Shader& operator=(Shader&& other) noexcept;
    
    void Use() const;
    void SetBool(const std::string& name, bool value) const;
    void SetInt(const std::string& name, int value) const;
    void SetFloat(const std::string& name, float value) const;
    void SetDouble(const std::string& name, double value) const;
    void SetVec2(const std::string& name, const glm::vec2& value) const;
    void SetVec2(const std::string& name, float x, float y) const;
    void SetVec3(const std::string& name, const glm::vec3& value) const;
    void SetVec3(const std::string& name, float x, float y, float z) const;
    void SetVec4(const std::string& name, const glm::vec4& value) const;
    void SetVec4(const std::string& name, float x, float y, float z, float w) const;
    void SetMat2(const std::string& name, const glm::mat2& mat) const;
    void SetMat3(const std::string& name, const glm::mat3& mat) const;
    void SetMat4(const std::string& name, const glm::mat4& mat) const;
    void SetVec2Double(const std::string& name, const glm::dvec2& value) const;
    void SetVec2Double(const std::string& name, double x, double y) const;
    void SetVec3Double(const std::string& name, const glm::dvec3& value) const;
    void SetVec3Double(const std::string& name, double x, double y, double z) const;
    void SetVec4Double(const std::string& name, const glm::dvec4& value) const;
    void SetVec4Double(const std::string& name, double x, double y, double z, double w) const;
    void SetMat2Double(const std::string& name, const glm::dmat2& mat) const;
    void SetMat3Double(const std::string& name, const glm::dmat3& mat) const;
    void SetMat4Double(const std::string& name, const glm::dmat4& mat) const;
    size_t GetProgramId() const;

private:
    enum class ShaderType {
        VertexShader,
        FragmentShader,
        GeometryShader,
        ShaderProgram
    };

    size_t _shaderProgramID = 0;
    // Lazy cache: first Set*/lookup resolves glGetUniformLocation; -1 misses are cached too.
    mutable std::unordered_map<std::string, GLint> _uniformLocationCache;

    GLint GetUniformLocation(const std::string& name) const;
    void Release();
    void Build(const std::string& vertexPath, const std::string& fragmentPath, const std::string& geometryPath);

    static void CheckCompileErrors(size_t shader, ShaderType type, const std::string& path = "");
    static std::string ShaderTypeToString(ShaderType type);
};

#endif //SOLARSYSTEM_SHADER_H
