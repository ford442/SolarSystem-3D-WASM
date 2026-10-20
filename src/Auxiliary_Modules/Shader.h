#ifndef SOLARSYSTEM_SHADER_H
#define SOLARSYSTEM_SHADER_H
#include "../gfx/GlHandle.h"
#include "ShaderSource.h"
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
    
    // Delete copy to prevent double-free
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;
    
    // Move semantics for proper ownership transfer
    Shader(Shader&& other) noexcept = default;
    Shader& operator=(Shader&& other) noexcept = default;
    
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

#ifndef __EMSCRIPTEN__
    // SOLARSYSTEM_SHADER_SELFTEST=1: compile every .vs/.fs under `directory` as its own
    // stage (assembled exactly as Build does) and report. Returns the failure count. This is
    // what CI runs under Mesa, so a shader that the running app would only reach after the
    // skybox loads (or never, on a given preset) still gets compiled.
    static int SelfTestDirectory(const std::string& directory);
#endif

private:
    enum class ShaderType {
        VertexShader,
        FragmentShader,
        GeometryShader,
        ShaderProgram
    };

    GlProgram _program;
    // Lazy cache: first Set*/lookup resolves glGetUniformLocation; -1 misses are cached too.
    mutable std::unordered_map<std::string, GLint> _uniformLocationCache;

    GLint GetUniformLocation(const std::string& name) const;
    void Build(const std::string& vertexPath, const std::string& fragmentPath, const std::string& geometryPath);

    static void CheckCompileErrors(size_t shader, ShaderType type, const std::string& path = "",
                                   const ShaderSource::Result* source = nullptr);
    static std::string ShaderTypeToString(ShaderType type);
};

#endif //SOLARSYSTEM_SHADER_H
