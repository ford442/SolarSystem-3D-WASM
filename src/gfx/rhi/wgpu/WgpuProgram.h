#ifndef SOLARSYSTEM_GFX_RHI_WGPU_WGPUPROGRAM_H
#define SOLARSYSTEM_GFX_RHI_WGPU_WGPUPROGRAM_H
#include <webgpu/webgpu_cpp.h>

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

/**
 * GLSL programs on WebGPU (docs/ARCHITECTURE.md §7.9): the WGSL tools/wgsl_shader_compiler
 * wrote for every stage permutation, and the manifest.json beside it, which says where each
 * stage's loose uniforms sit and which bindings it reads (gfx/rhi/WgslBindings.h numbering).
 *
 * A Program is the WebGPU side of one ShaderSpec: its two modules, the explicit bind group
 * layouts of its groups 0–2 and a CPU copy of each stage's uniform block. Set* mirrors
 * Shader::Set*: a name the stage does not use is a no-op, and setting the value it already has
 * marks nothing dirty. The command list copies dirty blocks into its per-frame uniform ring at
 * the next draw.
 */
namespace rhi::webgpu {

class Device;

enum class StageKind : std::uint8_t { Vertex, Fragment };

struct ManifestBinding {
    std::uint32_t group = 0;
    std::uint32_t binding = 0;
    enum class Kind : std::uint8_t { UniformBuffer, Texture, Sampler } kind = Kind::UniformBuffer;
    wgpu::TextureViewDimension viewDimension = wgpu::TextureViewDimension::e2D;
    int unit = -1;
};

struct ManifestUniform {
    std::uint32_t offset = 0;
    std::uint32_t glType = 0; // GL_FLOAT, GL_FLOAT_VEC3, GL_FLOAT_MAT4, … as glslang reflects them
    std::uint32_t count = 1;  // array length
};

struct ManifestStage {
    std::string wgsl; // file name beside the manifest
    StageKind stage = StageKind::Vertex;
    std::uint32_t uniformBlockSize = 0;
    std::unordered_map<std::string, ManifestUniform> uniforms;
    std::vector<ManifestBinding> bindings;
    std::vector<std::uint32_t> attributeLocations; // vertex stage
};

/** manifest.json: every translated stage permutation, keyed by its WGSL file name. */
class ShaderManifest {
public:
    /** Read `<directory>/manifest.json`; false with `error` if it is missing or malformed. */
    bool Load(const std::string& directory, std::string& error);
    const std::string& Directory() const { return _directory; }
    const ManifestStage* Find(const std::string& wgslName) const;
    /**
     * The WGSL file of `stageFile` ("hdr.fs") linked behind `vertexFile` ("passThrough.vs"),
     * keeping only the defines that stage declares: what tools/wgsl_shader_compiler named it.
     */
    std::string WgslName(const std::string& stageFile, const std::string& vertexFile,
                         const std::vector<std::string>& defines) const;
    std::size_t Size() const { return _stages.size(); }
    const std::map<std::string, ManifestStage>& Stages() const { return _stages; }

private:
    std::string _directory;
    std::map<std::string, ManifestStage> _stages;
    std::map<std::string, std::vector<std::string>> _declaredDefines; // per stage file
};

/** What a program is built from: stage files (paths or bare names) and permutation defines. */
struct ProgramDesc {
    std::string vertex;
    std::string fragment;
    std::vector<std::string> defines;
};

class Program {
public:
    /** Null (with `error`) if a stage has no WGSL or its module fails to compile. */
    static std::unique_ptr<Program> Create(Device& device, const ShaderManifest& manifest, const ProgramDesc& desc,
                                           std::string& error);

    std::uint64_t Id() const { return _id; }
    const std::string& Label() const { return _label; }
    const wgpu::ShaderModule& Module(StageKind stage) const { return _modules[Index(stage)]; }
    const wgpu::PipelineLayout& Layout() const { return _pipelineLayout; }
    const wgpu::BindGroupLayout& GroupLayout(std::uint32_t group) const { return _groupLayouts[group]; }
    /** The texture units a stage samples (group 1), in binding order. */
    const std::vector<int>& TextureUnits() const { return _textureUnits; }
    const std::vector<ManifestBinding>& FrameBindings() const { return _frameBindings; }
    const std::vector<std::uint32_t>& AttributeLocations() const { return _attributes; }

    void SetBool(std::string_view name, bool value) { SetInt(name, value ? 1 : 0); }
    void SetInt(std::string_view name, int value);
    void SetFloat(std::string_view name, float value);
    void SetVec2(std::string_view name, const glm::vec2& value);
    void SetVec3(std::string_view name, const glm::vec3& value);
    void SetVec4(std::string_view name, const glm::vec4& value);
    void SetMat3(std::string_view name, const glm::mat3& value);
    void SetMat4(std::string_view name, const glm::mat4& value);

    /** The stage's std140 block (empty if it has none) and whether it changed since the last take. */
    const std::vector<std::uint8_t>& Block(StageKind stage) const { return _blocks[Index(stage)]; }
    bool TakeDirty() {
        const bool dirty = _dirty;
        _dirty = false;
        return dirty;
    }

private:
    static std::size_t Index(StageKind stage) { return stage == StageKind::Vertex ? 0 : 1; }
    /** Write `bytes` (one element's worth, already in its std140 shape) wherever `name` lives. */
    void Write(std::string_view name, const void* bytes, std::size_t size, std::uint32_t columns = 1,
               std::size_t columnBytes = 0);

    std::uint64_t _id = 0;
    std::string _label;
    std::array<wgpu::ShaderModule, 2> _modules;
    std::array<wgpu::BindGroupLayout, 3> _groupLayouts;
    wgpu::PipelineLayout _pipelineLayout;
    std::array<std::vector<std::uint8_t>, 2> _blocks;
    std::array<std::unordered_map<std::string, ManifestUniform>, 2> _uniforms;
    std::vector<int> _textureUnits;
    std::vector<ManifestBinding> _frameBindings;
    std::vector<std::uint32_t> _attributes;
    bool _dirty = true;
};

} // namespace rhi::webgpu

#endif // SOLARSYSTEM_GFX_RHI_WGPU_WGPUPROGRAM_H
