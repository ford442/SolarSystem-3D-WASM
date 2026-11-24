#include "WgpuSelfTest.h"
#include "WgpuStrings.h"
#include "WgpuDevice.h"
#include "WgpuProgram.h"
#include "core/Json.h"
#include "core/Log.h"

#include <webgpu/webgpu_cpp.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace rhi::webgpu {

namespace {

/** The validation error `create` raised, if any (native: waits on the error scope). */
template <typename F>
std::string CatchValidation(Device& device, F&& create) {
    device.Get().PushErrorScope(wgpu::ErrorFilter::Validation);
    create();
    std::string message;
    const wgpu::Future future = device.Get().PopErrorScope(
        wgpu::CallbackMode::WaitAnyOnly, [&](wgpu::PopErrorScopeStatus, wgpu::ErrorType type, wgpu::StringView text) {
            if (type != wgpu::ErrorType::NoError) {
                message = ToString(text);
            }
        });
    device.Wait(future);
    return message;
}

wgpu::VertexFormat AttributeFormat(const std::string& wgslType) {
    if (wgslType.find("i32") != std::string::npos) {
        return wgpu::VertexFormat::Sint32x4;
    }
    if (wgslType.find("u32") != std::string::npos) {
        return wgpu::VertexFormat::Uint32x4;
    }
    return wgpu::VertexFormat::Float32x4; // WebGPU fills or drops components to the shader's type
}

} // namespace

int SelfTestWgsl(Device& device, const std::string& directory) {
    ShaderManifest manifest;
    std::string error;
    if (!manifest.Load(directory, error)) {
        LOG_ERROR("ShaderSelfTest", "WGSL: %s", error.c_str());
        return 1;
    }
    Json::Value root;
    if (!Json::ParseFile(directory + "/manifest.json", root, error)) {
        LOG_ERROR("ShaderSelfTest", "WGSL: %s", error.c_str());
        return 1;
    }

    int failures = 0;
    std::size_t linked = 0;
    for (const Json::Value& entry : root["stages"].Items()) {
        if (!entry["ok"].AsBool() || entry["stage"].AsString("") != "fragment") {
            continue;
        }
        ProgramDesc desc;
        desc.vertex = entry["vertex"].AsString("");
        desc.fragment = entry["file"].AsString("");
        for (const Json::Value& define : entry["defines"].Items()) {
            desc.defines.push_back(define.AsString());
        }
        std::unique_ptr<Program> program;
        std::string message =
            CatchValidation(device, [&] { program = Program::Create(device, manifest, desc, error); });
        if (!program) {
            LOG_ERROR("ShaderSelfTest", "WGSL %s: %s", entry["wgsl"].AsString("").c_str(), error.c_str());
            ++failures;
            continue;
        }

        // One buffer slot per attribute, so any location set the vertex stage reads is covered.
        const ManifestStage* vertex = manifest.Find(manifest.WgslName(desc.vertex, {}, desc.defines));
        std::vector<wgpu::VertexAttribute> attributes;
        std::vector<wgpu::VertexBufferLayout> buffers;
        for (const Json::Value& stage : root["stages"].Items()) {
            if (vertex && stage["wgsl"].AsString("") == vertex->wgsl) {
                for (const Json::Value& attribute : stage["attributes"].Items()) {
                    wgpu::VertexAttribute a{};
                    a.format = AttributeFormat(attribute["type"].AsString(""));
                    a.shaderLocation = static_cast<std::uint32_t>(attribute["location"].AsNumber(0.0));
                    attributes.push_back(a);
                }
            }
        }
        buffers.resize(attributes.size());
        for (std::size_t i = 0; i < attributes.size(); ++i) {
            buffers[i].arrayStride = 16;
            buffers[i].attributeCount = 1;
            buffers[i].attributes = &attributes[i];
        }

        wgpu::ColorTargetState color{};
        color.format = wgpu::TextureFormat::RGBA16Float;
        wgpu::FragmentState fragment{};
        fragment.module = program->Module(StageKind::Fragment);
        fragment.entryPoint = "main";
        // A depth-only stage (shadowMap.fs) links against a depth target alone.
        const bool colorOutput = entry["colorOutputs"].AsNumber(1.0) > 0.0;
        fragment.targetCount = colorOutput ? 1 : 0;
        fragment.targets = colorOutput ? &color : nullptr;
        wgpu::DepthStencilState depth{};
        depth.format = wgpu::TextureFormat::Depth24Plus;
        depth.depthWriteEnabled = wgpu::OptionalBool::True;
        depth.depthCompare = wgpu::CompareFunction::Less;
        wgpu::RenderPipelineDescriptor pipeline{};
        pipeline.label = program->Label().c_str();
        pipeline.layout = program->Layout();
        pipeline.vertex.module = program->Module(StageKind::Vertex);
        pipeline.vertex.entryPoint = "main";
        pipeline.vertex.bufferCount = buffers.size();
        pipeline.vertex.buffers = buffers.data();
        pipeline.depthStencil = &depth;
        pipeline.fragment = &fragment;
        message += CatchValidation(device, [&] { (void)device.Get().CreateRenderPipeline(&pipeline); });
        if (!message.empty()) {
            LOG_ERROR("ShaderSelfTest", "WGSL %s does not link: %s", program->Label().c_str(), message.c_str());
            ++failures;
            continue;
        }
        ++linked;
    }
    LOG_INFO("ShaderSelfTest", "%zu/%zu WGSL programs compiled and linked on %s%s", linked,
             linked + static_cast<std::size_t>(failures), device.AdapterName().c_str(),
             failures == 0 ? " OK" : " - FAILURES");
    return failures;
}

} // namespace rhi::webgpu
