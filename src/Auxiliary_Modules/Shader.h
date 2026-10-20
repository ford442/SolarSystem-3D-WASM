#ifndef SOLARSYSTEM_SHADER_H
#define SOLARSYSTEM_SHADER_H
#include "../gfx/GlHandle.h"
#include "ShaderSource.h"
#include <glm/glm.hpp>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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
    // Names are looked up in the table Build() fills from the linked program's active
    // uniforms: no GL query and no allocation per call. A name the program does not use
    // (optimised out, or not declared) is a silent no-op, as glUniform* with -1 would be;
    // so is setting the value the uniform already has.
    void SetBool(std::string_view name, bool value) const;
    void SetInt(std::string_view name, int value) const;
    void SetFloat(std::string_view name, float value) const;
    void SetVec2(std::string_view name, const glm::vec2& value) const;
    void SetVec2(std::string_view name, float x, float y) const;
    void SetVec3(std::string_view name, const glm::vec3& value) const;
    void SetVec3(std::string_view name, float x, float y, float z) const;
    void SetVec4(std::string_view name, const glm::vec4& value) const;
    void SetMat3(std::string_view name, const glm::mat3& mat) const;
    void SetMat4(std::string_view name, const glm::mat4& mat) const;
    /** Whether the program reads FrameUBO (common/frame.glsl), bound at FrameUniformBuffer::kBinding. */
    bool UsesFrameUniforms() const { return _usesFrameUniforms; }

    /** glUniform* calls issued by every Shader since the last reset (frame stats). */
    static unsigned long long UniformCallCount() { return s_uniformCalls; }
    static void ResetUniformCallCount() { s_uniformCalls = 0; }

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
    // One per active default-block uniform: its location and the last value this Shader
    // gave it. A program's uniforms only change through these setters, so a Set* with the
    // value already there skips the GL call (bodies sharing a program mostly repeat their
    // flags, and every label repeats the text state).
    struct UniformSlot {
        GLint location = -1;
        unsigned char size = 0; // bytes of `value` in use; 0 = never set
        alignas(16) unsigned char value[64] = {};
    };
    mutable std::vector<UniformSlot> _slots;
    // Name → slot, sorted by name; arrays are listed under "name" and "name[0]" (same slot).
    std::vector<std::pair<std::string, unsigned int>> _uniformIndex;
    bool _usesFrameUniforms = false;
    static inline unsigned long long s_uniformCalls = 0;
    static inline GLuint s_currentProgram = 0; // what the last Use() bound

    /** The slot for `name`, or null when the program has no such active uniform. */
    UniformSlot* FindSlot(std::string_view name) const;
    /** Records `value` in `slot`; false when it already held exactly that value. */
    template <class T>
    static bool Store(UniformSlot& slot, const T& value);
    void Build(const std::string& vertexPath, const std::string& fragmentPath, const std::string& geometryPath);
    /** After linking: the location table, sampler units from `binding =`, and FrameUBO's binding. */
    void Introspect(const std::vector<ShaderSource::SamplerBinding>& samplerBindings);

    static void CheckCompileErrors(size_t shader, ShaderType type, const std::string& path = "",
                                   const ShaderSource::Result* source = nullptr);
    static std::string ShaderTypeToString(ShaderType type);
};

#endif //SOLARSYSTEM_SHADER_H
