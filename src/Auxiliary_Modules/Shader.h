#ifndef SOLARSYSTEM_SHADER_H
#define SOLARSYSTEM_SHADER_H
#include "../gfx/GlHandle.h"
#include "ShaderSource.h"
#include <glm/glm.hpp>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

/**
 * What a program is built from: its stage files and permutation defines. Also the
 * ShaderLibrary cache key, so the defines are kept sorted and unique.
 */
struct ShaderSpec {
    std::string vertex;
    std::string fragment;
    std::string geometry; // native only; empty = no geometry stage
    std::vector<std::string> defines;

    ShaderSpec() = default;
    ShaderSpec(std::string vertexPath, std::string fragmentPath, std::vector<std::string> permutationDefines = {},
               std::string geometryPath = {});

    bool operator<(const ShaderSpec& other) const {
        return std::tie(vertex, fragment, geometry, defines) <
               std::tie(other.vertex, other.fragment, other.geometry, other.defines);
    }
    bool operator==(const ShaderSpec& other) const {
        return std::tie(vertex, fragment, geometry, defines) ==
               std::tie(other.vertex, other.fragment, other.geometry, other.defines);
    }
    /** "planetLighting.vs + planetLighting.fs [HAS_CLOUDS]" for logs. */
    std::string Describe() const;
};

class Shader {
public:
    /** Compile and link now; throws std::runtime_error (after printing the log) on failure. */
    explicit Shader(ShaderSpec spec);
    Shader(const std::string& vertexPath, const std::string& fragmentPath, std::vector<std::string> defines = {});
#ifndef __EMSCRIPTEN__
    // Geometry shaders are desktop-only: WebGL 2 / GLES 3.0 have no geometry stage, so
    // this overload does not exist in the Emscripten build and passing a geometry path
    // there is a compile error rather than a shader that silently fails to link.
    Shader(const std::string& vertexPath, const std::string& fragmentPath, const std::string& geometryPath);
#endif
    /**
     * Hand the stages to the driver and link, without waiting for either. With
     * KHR_parallel_shader_compile the driver compiles in the background, so submitting every
     * program before finishing any overlaps their compiles. Throws only on a preprocess error.
     */
    static Shader Submit(ShaderSpec spec);
    ~Shader();

    // Delete copy to prevent double-free
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    // Move semantics for proper ownership transfer. Every live Shader is in the hot-reload
    // registry, so the move constructor registers the new object.
    Shader(Shader&& other) noexcept;
    Shader& operator=(Shader&& other) noexcept;

    /** False while a submitted program is still compiling (always true without the extension). */
    bool IsReady() const;
    /** Wait for a submitted program, check it and build the uniform table; throws on failure. */
    void Finish();
    bool IsFinished() const { return _pending == nullptr; }
    const ShaderSpec& Spec() const { return _spec; }

    /** Binds the program; finishes a submitted one first. */
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

    /**
     * Hot reload: rebuild every live Shader one of whose source files (includes too) changed
     * on disk since it was built. A rebuilt Shader keeps its address and its uniform values;
     * one that fails to compile keeps the old program and prints the error. Call between
     * frames. Returns how many programs were replaced.
     */
    static int ReloadChanged();
    /** Every file some live Shader was built from (deduplicated; what the web dev hook re-fetches). */
    static std::vector<std::string> LiveSourceFiles();

#ifndef __EMSCRIPTEN__
    // SOLARSYSTEM_SHADER_SELFTEST=1: compile every .vs/.fs under `directory` as its own
    // stage (assembled exactly as Build does) and report. Returns the failure count. This is
    // what CI runs under Mesa, so a shader that the running app would only reach after the
    // skybox loads (or never, on a given preset) still gets compiled. A stage that declares
    // `// permutations: A B …` is compiled once per subset of those defines.
    static int SelfTestDirectory(const std::string& directory);
#endif

private:
    enum class ShaderType {
        VertexShader,
        FragmentShader,
        GeometryShader,
        ShaderProgram
    };

    Shader(); // registers; the other constructors delegate here
    struct Pending;

    ShaderSpec _spec;
    GlProgram _program;
    // Submitted but not yet Finish()ed: the stages and their assembled source (error reports).
    mutable std::unique_ptr<Pending> _pending;
    // What the program was assembled from and each file's mtime then (hot reload).
    std::vector<std::pair<std::string, long long>> _sourceFiles;
    // One per active default-block uniform: its location and the last value this Shader
    // gave it. A program's uniforms only change through these setters, so a Set* with the
    // value already there skips the GL call (bodies sharing a program mostly repeat their
    // flags, and every label repeats the text state).
    struct UniformSlot {
        GLint location = -1;
        GLenum type = 0;
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
    /** Issue `slot`'s stored value to GL by its type (hot reload replays them). */
    void Apply(const UniformSlot& slot) const;
    /** Run `issue` with this program current (glUniform* targets the current program). */
    template <class F>
    void WithProgramBound(F&& issue) const;
    void SubmitStages();
    /** After linking: the location table, sampler units from `binding =`, and FrameUBO's binding. */
    void Introspect(const std::vector<ShaderSource::SamplerBinding>& samplerBindings);
    /** Whether a recorded source file's mtime moved; `cache` dedupes stats across Shaders. */
    bool SourcesChanged(std::vector<std::pair<std::string, long long>>& cache) const;
    void RefreshSourceTimes();
    /** Rebuild from _spec; on success take the new program and replay the uniform values. */
    bool Reload();

    static std::vector<Shader*>& Registry();
    static void CheckCompileErrors(size_t shader, ShaderType type, const std::string& path = "",
                                   const ShaderSource::Result* source = nullptr);
    static std::string ShaderTypeToString(ShaderType type);
};

#endif //SOLARSYSTEM_SHADER_H
