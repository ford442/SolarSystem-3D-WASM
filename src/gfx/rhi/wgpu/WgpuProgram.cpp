#include "WgpuProgram.h"
#include "WgpuStrings.h"
#include "WgpuDevice.h"
#include "core/Json.h"
#include "gfx/ShaderSource.h"
#include "gfx/rhi/WgslBindings.h"

#include <webgpu/webgpu_cpp.h>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace rhi::webgpu {

namespace {

// GL type enums, as glslang's reflection reports them (GL/glcorearb.h values).
constexpr std::uint32_t kGlFloatMat2 = 0x8B5A;
constexpr std::uint32_t kGlFloatMat3 = 0x8B5B;
constexpr std::uint32_t kGlFloatMat4 = 0x8B5C;

/** std140 array stride of one element: every element starts on a vec4 boundary. */
std::uint32_t ArrayStride(std::uint32_t glType) {
    switch (glType) {
        case kGlFloatMat2: return 32;
        case kGlFloatMat3: return 48;
        case kGlFloatMat4: return 64;
        default: return 16;
    }
}

wgpu::TextureViewDimension ViewDimension(const std::string& wgslType) {
    if (wgslType.rfind("texture_cube", 0) == 0) {
        return wgpu::TextureViewDimension::Cube;
    }
    if (wgslType.rfind("texture_3d", 0) == 0) {
        return wgpu::TextureViewDimension::e3D;
    }
    if (wgslType.rfind("texture_2d_array", 0) == 0) {
        return wgpu::TextureViewDimension::e2DArray;
    }
    return wgpu::TextureViewDimension::e2D;
}

std::string FileName(const std::string& path) {
    const std::size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string Stem(const std::string& file) {
    return file.substr(0, file.rfind('.'));
}

std::atomic<std::uint64_t> g_nextProgramId{1};

} // namespace

bool ShaderManifest::Load(const std::string& directory, std::string& error) {
    Json::Value root;
    if (!Json::ParseFile(directory + "/manifest.json", root, error)) {
        return false;
    }
    _directory = directory;
    _stages.clear();
    _declaredDefines.clear();
    for (const Json::Value& entry : root["stages"].Items()) {
        if (!entry["ok"].AsBool()) {
            continue; // failed or replaced on WebGPU: nothing to load
        }
        ManifestStage stage;
        stage.wgsl = entry["wgsl"].AsString("");
        stage.stage = entry["stage"].AsString("") == "vertex" ? StageKind::Vertex : StageKind::Fragment;
        stage.uniformBlockSize = static_cast<std::uint32_t>(entry["uniformBlockSize"].AsNumber(0.0));
        for (const Json::Value& uniform : entry["uniforms"].Items()) {
            ManifestUniform slot;
            slot.offset = static_cast<std::uint32_t>(uniform["offset"].AsNumber(0.0));
            slot.glType = static_cast<std::uint32_t>(std::stoul(uniform["type"].AsString("0"), nullptr, 16));
            slot.count = static_cast<std::uint32_t>(uniform["count"].AsNumber(1.0));
            stage.uniforms.emplace(uniform["name"].AsString(""), slot);
        }
        for (const Json::Value& binding : entry["bindings"].Items()) {
            ManifestBinding b;
            b.group = static_cast<std::uint32_t>(binding["group"].AsNumber(0.0));
            b.binding = static_cast<std::uint32_t>(binding["binding"].AsNumber(0.0));
            const std::string kind = binding["kind"].AsString("");
            b.kind = kind == "texture"   ? ManifestBinding::Kind::Texture
                     : kind == "sampler" ? ManifestBinding::Kind::Sampler
                                         : ManifestBinding::Kind::UniformBuffer;
            b.viewDimension = ViewDimension(binding["type"].AsString(""));
            b.unit = static_cast<int>(binding["unit"].AsNumber(-1.0));
            stage.bindings.push_back(b);
        }
        for (const Json::Value& attribute : entry["attributes"].Items()) {
            stage.attributeLocations.push_back(static_cast<std::uint32_t>(attribute["location"].AsNumber(0.0)));
        }
        std::vector<std::string>& declared = _declaredDefines[entry["file"].AsString("")];
        for (const Json::Value& define : entry["defines"].Items()) {
            if (std::find(declared.begin(), declared.end(), define.AsString()) == declared.end()) {
                declared.push_back(define.AsString());
            }
        }
        _stages.emplace(stage.wgsl, std::move(stage));
    }
    if (_stages.empty()) {
        error = directory + "/manifest.json lists no translated stage";
        return false;
    }
    return true;
}

const ManifestStage* ShaderManifest::Find(const std::string& wgslName) const {
    const auto it = _stages.find(wgslName);
    return it == _stages.end() ? nullptr : &it->second;
}

std::string ShaderManifest::WgslName(const std::string& stageFile, const std::string& vertexFile,
                                     const std::vector<std::string>& defines) const {
    const std::string file = FileName(stageFile);
    std::string name = file;
    const std::string vertex = FileName(vertexFile);
    if (!vertex.empty() && file != vertex && Stem(vertex) != Stem(file)) {
        name += "@" + vertex;
    }
    const auto declared = _declaredDefines.find(file);
    std::vector<std::string> kept;
    for (const std::string& define : defines) {
        if (declared != _declaredDefines.end() &&
            std::find(declared->second.begin(), declared->second.end(), define) != declared->second.end()) {
            kept.push_back(define);
        }
    }
    std::sort(kept.begin(), kept.end());
    for (const std::string& define : kept) {
        name += "." + define;
    }
    return name + ".wgsl";
}

std::unique_ptr<Program> Program::Create(Device& device, const ShaderManifest& manifest, const ProgramDesc& desc,
                                         std::string& error) {
    std::unique_ptr<Program> program(new Program());
    program->_id = g_nextProgramId++;
    const std::string vertexName = manifest.WgslName(desc.vertex, {}, desc.defines);
    const std::string fragmentName = manifest.WgslName(desc.fragment, desc.vertex, desc.defines);
    program->_label = vertexName + " + " + fragmentName;
    const std::array<const ManifestStage*, 2> stages{manifest.Find(vertexName), manifest.Find(fragmentName)};
    for (std::size_t i = 0; i < 2; ++i) {
        if (!stages[i]) {
            error = "no WGSL for " + (i == 0 ? vertexName : fragmentName) + " in " + manifest.Directory() +
                    " (run wgsl_shader_compiler; a fragment stage behind another vertex stage needs "
                    "a `// vertex:` line)";
            return nullptr;
        }
        const std::optional<std::string> code =
            ShaderSource::ReadFileFromDisk(manifest.Directory() + "/" + stages[i]->wgsl);
        if (!code) {
            error = "cannot read " + manifest.Directory() + "/" + stages[i]->wgsl;
            return nullptr;
        }
        wgpu::ShaderSourceWGSL wgsl{};
        wgsl.code = code->c_str();
        wgpu::ShaderModuleDescriptor moduleDescriptor{};
        moduleDescriptor.nextInChain = &wgsl;
        moduleDescriptor.label = stages[i]->wgsl.c_str();
        program->_modules[i] = device.Get().CreateShaderModule(&moduleDescriptor);
        program->_blocks[i].assign(stages[i]->uniformBlockSize, 0);
        program->_uniforms[i] = stages[i]->uniforms;
    }
    program->_attributes = stages[0]->attributeLocations;
    std::sort(program->_attributes.begin(), program->_attributes.end());

    // Explicit layouts, so pipelines of one program (one per fixed-function state) share bind groups.
    std::array<std::vector<wgpu::BindGroupLayoutEntry>, 3> entries;
    std::map<std::pair<std::uint32_t, std::uint32_t>, std::size_t> seen; // (group, binding) → index
    for (std::size_t i = 0; i < 2; ++i) {
        const wgpu::ShaderStage visibility = i == 0 ? wgpu::ShaderStage::Vertex : wgpu::ShaderStage::Fragment;
        for (const ManifestBinding& binding : stages[i]->bindings) {
            if (binding.group > rhi::wgsl::kUniformGroup) {
                error = program->_label + ": binding in unexpected group " + std::to_string(binding.group);
                return nullptr;
            }
            const auto key = std::make_pair(binding.group, binding.binding);
            if (const auto it = seen.find(key); it != seen.end()) {
                entries[binding.group][it->second].visibility |= visibility;
                continue;
            }
            wgpu::BindGroupLayoutEntry entry{};
            entry.binding = binding.binding;
            entry.visibility = visibility;
            switch (binding.kind) {
                case ManifestBinding::Kind::UniformBuffer:
                    entry.buffer.type = wgpu::BufferBindingType::Uniform;
                    entry.buffer.hasDynamicOffset = binding.group == rhi::wgsl::kUniformGroup;
                    break;
                case ManifestBinding::Kind::Texture:
                    entry.texture.sampleType = wgpu::TextureSampleType::Float;
                    entry.texture.viewDimension = binding.viewDimension;
                    program->_textureUnits.push_back(binding.unit);
                    break;
                case ManifestBinding::Kind::Sampler: entry.sampler.type = wgpu::SamplerBindingType::Filtering; break;
            }
            if (binding.group == rhi::wgsl::kFrameGroup) {
                program->_frameBindings.push_back(binding);
            }
            seen.emplace(key, entries[binding.group].size());
            entries[binding.group].push_back(entry);
        }
    }
    std::sort(program->_textureUnits.begin(), program->_textureUnits.end());
    program->_textureUnits.erase(std::unique(program->_textureUnits.begin(), program->_textureUnits.end()),
                                 program->_textureUnits.end());
    for (std::size_t group = 0; group < 3; ++group) {
        wgpu::BindGroupLayoutDescriptor layoutDescriptor{};
        layoutDescriptor.entryCount = entries[group].size();
        layoutDescriptor.entries = entries[group].data();
        program->_groupLayouts[group] = device.Get().CreateBindGroupLayout(&layoutDescriptor);
    }
    wgpu::PipelineLayoutDescriptor pipelineLayout{};
    pipelineLayout.bindGroupLayoutCount = program->_groupLayouts.size();
    pipelineLayout.bindGroupLayouts = program->_groupLayouts.data();
    program->_pipelineLayout = device.Get().CreatePipelineLayout(&pipelineLayout);

#ifndef __EMSCRIPTEN__
    // Natively, report a WGSL module that does not compile now rather than at the first draw.
    for (std::size_t i = 0; i < 2; ++i) {
        std::string messages;
        bool failed = false;
        const wgpu::Future future = program->_modules[i].GetCompilationInfo(
            wgpu::CallbackMode::WaitAnyOnly,
            [&](wgpu::CompilationInfoRequestStatus, const wgpu::CompilationInfo* info) {
                for (std::size_t m = 0; info && m < info->messageCount; ++m) {
                    if (info->messages[m].type == wgpu::CompilationMessageType::Error) {
                        failed = true;
                        messages += ToString(info->messages[m].message) + "\n";
                    }
                }
            });
        device.Wait(future);
        if (failed) {
            error = stages[i]->wgsl + ": " + messages;
            return nullptr;
        }
    }
#endif
    return program;
}

void Program::Write(std::string_view name, const void* bytes, std::size_t size, std::uint32_t columns,
                    std::size_t columnBytes) {
    // "lights[2]" addresses element 2 of the array `lights`.
    std::string_view base = name;
    std::uint32_t element = 0;
    if (const std::size_t open = name.find('['); open != std::string_view::npos && name.back() == ']') {
        base = name.substr(0, open);
        element = static_cast<std::uint32_t>(std::stoul(std::string(name.substr(open + 1, name.size() - open - 2))));
    }
    for (std::size_t stage = 0; stage < 2; ++stage) {
        const auto it = _uniforms[stage].find(std::string(base));
        if (it == _uniforms[stage].end() || element >= it->second.count) {
            continue; // not used by this stage (or optimised out), as glUniform* with -1 would be
        }
        std::vector<std::uint8_t>& block = _blocks[stage];
        const std::size_t offset =
            it->second.offset + static_cast<std::size_t>(element) * ArrayStride(it->second.glType);
        const auto* source = static_cast<const std::uint8_t*>(bytes);
        for (std::uint32_t column = 0; column < columns; ++column) {
            // Matrix columns start on vec4 boundaries in std140 (a mat3 column is 12 of 16 bytes).
            const std::size_t at = offset + column * 16;
            const std::size_t length = columns > 1 ? columnBytes : size;
            if (at + length > block.size()) {
                break;
            }
            if (std::memcmp(block.data() + at, source + column * length, length) != 0) {
                std::memcpy(block.data() + at, source + column * length, length);
                _dirty = true;
            }
        }
    }
}

void Program::SetInt(std::string_view name, int value) {
    const std::int32_t v = value;
    Write(name, &v, sizeof v);
}

void Program::SetFloat(std::string_view name, float value) {
    Write(name, &value, sizeof value);
}
void Program::SetVec2(std::string_view name, const glm::vec2& value) {
    Write(name, glm::value_ptr(value), 8);
}
void Program::SetVec3(std::string_view name, const glm::vec3& value) {
    Write(name, glm::value_ptr(value), 12);
}
void Program::SetVec4(std::string_view name, const glm::vec4& value) {
    Write(name, glm::value_ptr(value), 16);
}
void Program::SetMat3(std::string_view name, const glm::mat3& value) {
    Write(name, glm::value_ptr(value), 36, 3, 12);
}
void Program::SetMat4(std::string_view name, const glm::mat4& value) {
    Write(name, glm::value_ptr(value), 64, 4, 16);
}

} // namespace rhi::webgpu
