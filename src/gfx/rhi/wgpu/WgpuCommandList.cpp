#include "WgpuCommandList.h"
#include "WgpuDevice.h"
#include "WgpuProgram.h"
#include "core/FrameProfile.h"
#include "core/Log.h"
#include "gfx/rhi/PipelineTracker.h"
#include "gfx/rhi/RenderPassPlanner.h"
#include "gfx/rhi/RhiTypes.h"
#include "gfx/rhi/TimestampPairs.h"
#include "gfx/rhi/WgslBindings.h"

#include <webgpu/webgpu_cpp.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace rhi::webgpu {

namespace {

constexpr std::size_t kUniformRingBytes = 4u << 20; // per frame; a Full frame uses well under 1 MB
constexpr std::uint32_t kTimestampPairs = 512;      // render passes timed per frame

wgpu::TextureFormat ToWgpu(TextureFormat format) {
    switch (format) {
        case TextureFormat::Rgba8: return wgpu::TextureFormat::RGBA8Unorm;
        case TextureFormat::Rgba16F: return wgpu::TextureFormat::RGBA16Float;
        case TextureFormat::Depth24: return wgpu::TextureFormat::Depth24Plus;
        case TextureFormat::Depth32F: return wgpu::TextureFormat::Depth32Float;
        case TextureFormat::Unknown:
        case TextureFormat::Buffer: break;
    }
    return wgpu::TextureFormat::Undefined;
}

std::uint32_t BytesPerTexel(TextureFormat format) {
    switch (format) {
        case TextureFormat::Rgba8: return 4;
        case TextureFormat::Rgba16F: return 8;
        case TextureFormat::Depth32F: return 4;
        default: return 0;
    }
}

wgpu::BlendFactor ToWgpu(BlendFactor factor) {
    switch (factor) {
        case BlendFactor::Zero: return wgpu::BlendFactor::Zero;
        case BlendFactor::One: return wgpu::BlendFactor::One;
        case BlendFactor::SrcAlpha: return wgpu::BlendFactor::SrcAlpha;
        case BlendFactor::OneMinusSrcAlpha: return wgpu::BlendFactor::OneMinusSrcAlpha;
        case BlendFactor::OneMinusSrcColor: return wgpu::BlendFactor::OneMinusSrc;
    }
    return wgpu::BlendFactor::One;
}

wgpu::CompareFunction ToWgpu(CompareOp op) {
    switch (op) {
        case CompareOp::Less: return wgpu::CompareFunction::Less;
        case CompareOp::LessEqual: return wgpu::CompareFunction::LessEqual;
        case CompareOp::Always: return wgpu::CompareFunction::Always;
    }
    return wgpu::CompareFunction::Always;
}

wgpu::PrimitiveTopology ToWgpu(PrimitiveTopology topology) {
    switch (topology) {
        case PrimitiveTopology::Triangles: return wgpu::PrimitiveTopology::TriangleList;
        case PrimitiveTopology::TriangleStrip: return wgpu::PrimitiveTopology::TriangleStrip;
        case PrimitiveTopology::Lines: return wgpu::PrimitiveTopology::LineList;
        case PrimitiveTopology::LineStrip: return wgpu::PrimitiveTopology::LineStrip;
        case PrimitiveTopology::Points: return wgpu::PrimitiveTopology::PointList;
    }
    return wgpu::PrimitiveTopology::TriangleList;
}

wgpu::VertexFormat FloatFormat(std::uint8_t components) {
    switch (components) {
        case 1: return wgpu::VertexFormat::Float32;
        case 2: return wgpu::VertexFormat::Float32x2;
        case 3: return wgpu::VertexFormat::Float32x3;
        default: return wgpu::VertexFormat::Float32x4;
    }
}

std::uint32_t AlignUp(std::uint32_t value, std::uint32_t alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

// Present: backbuffer row y (bottom-up, GL order) to surface row height-1-y (top-down).
constexpr const char* kBlitWgsl = R"(
@group(0) @binding(0) var source : texture_2d<f32>;

@vertex
fn vs(@builtin(vertex_index) index : u32) -> @builtin(position) vec4<f32> {
    let corner = vec2<f32>(f32((index << 1u) & 2u), f32(index & 2u));
    return vec4<f32>(corner * 2.0 - 1.0, 0.0, 1.0);
}

@fragment
fn fs(@builtin(position) position : vec4<f32>) -> @location(0) vec4<f32> {
    let size = vec2<i32>(textureDimensions(source));
    let texel = vec2<i32>(i32(position.x), size.y - 1 - i32(position.y));
    return vec4<f32>(textureLoad(source, texel, 0).rgb, 1.0);
}
)";

} // namespace

CommandList::CommandList(Device& device) : _device(&device), _planner(*this) {
    _ring.staging.resize(kUniformRingBytes);
    wgpu::BufferDescriptor ring{};
    ring.label = "uniform ring";
    ring.size = kUniformRingBytes;
    ring.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
    _ring.buffer = device.Get().CreateBuffer(&ring);

    if (device.HasTimestampQuery()) {
        wgpu::QuerySetDescriptor querySet{};
        querySet.type = wgpu::QueryType::Timestamp;
        querySet.count = 2 * kTimestampPairs;
        _timestamps.querySet = device.Get().CreateQuerySet(&querySet);
        wgpu::BufferDescriptor resolve{};
        resolve.size = 2 * kTimestampPairs * sizeof(std::uint64_t);
        resolve.usage = wgpu::BufferUsage::QueryResolve | wgpu::BufferUsage::CopySrc;
        _timestamps.resolve = device.Get().CreateBuffer(&resolve);
        for (wgpu::Buffer& readback : _timestamps.readback) {
            wgpu::BufferDescriptor read{};
            read.size = resolve.size;
            read.usage = wgpu::BufferUsage::MapRead | wgpu::BufferUsage::CopyDst;
            readback = device.Get().CreateBuffer(&read);
        }
        _timestamps.pairs = {TimestampPairs(kTimestampPairs), TimestampPairs(kTimestampPairs)};
    }

    wgpu::ShaderSourceWGSL blit{};
    blit.code = kBlitWgsl;
    wgpu::ShaderModuleDescriptor blitModule{};
    blitModule.nextInChain = &blit;
    blitModule.label = "present blit";
    _blitModule = device.Get().CreateShaderModule(&blitModule);
    wgpu::BindGroupLayoutEntry source{};
    source.binding = 0;
    source.visibility = wgpu::ShaderStage::Fragment;
    source.texture.sampleType = wgpu::TextureSampleType::UnfilterableFloat;
    source.texture.viewDimension = wgpu::TextureViewDimension::e2D;
    wgpu::BindGroupLayoutDescriptor blitLayout{};
    blitLayout.entryCount = 1;
    blitLayout.entries = &source;
    _blitLayout = device.Get().CreateBindGroupLayout(&blitLayout);
}

CommandList::~CommandList() {
    // A timestamp readback still mapping calls back into this object; end it while it is whole.
    for (wgpu::Buffer& readback : _timestamps.readback) {
        if (readback) {
            readback.Destroy();
        }
    }
    _device->Poll();
}

wgpu::Sampler CommandList::SamplerFor(Filter filter) {
    wgpu::Sampler& sampler = _samplers[filter == Filter::Linear ? 0 : 1];
    if (!sampler) {
        wgpu::SamplerDescriptor descriptor{};
        descriptor.addressModeU = wgpu::AddressMode::ClampToEdge;
        descriptor.addressModeV = wgpu::AddressMode::ClampToEdge;
        descriptor.addressModeW = wgpu::AddressMode::ClampToEdge;
        descriptor.magFilter = filter == Filter::Linear ? wgpu::FilterMode::Linear : wgpu::FilterMode::Nearest;
        descriptor.minFilter = descriptor.magFilter;
        sampler = _device->Get().CreateSampler(&descriptor);
    }
    return sampler;
}

ObjectId CommandList::CreateTexture(const TextureDesc& desc, Filter filter) {
    wgpu::TextureDescriptor descriptor{};
    descriptor.size = {desc.width, desc.height, 1};
    descriptor.format = ToWgpu(desc.format);
    descriptor.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst | wgpu::TextureUsage::CopySrc |
                       wgpu::TextureUsage::RenderAttachment;
    Texture texture;
    texture.texture = _device->Get().CreateTexture(&descriptor);
    texture.view = texture.texture.CreateView();
    texture.sampler = SamplerFor(filter);
    texture.desc = desc;
    _textures.push_back(texture);
    return static_cast<ObjectId>(_textures.size());
}

void CommandList::WriteTexture(ObjectId id, const void* data, std::size_t bytesPerRow) {
    const Texture* texture = FindTexture(id);
    if (!texture) {
        return;
    }
    wgpu::TexelCopyTextureInfo destination{};
    destination.texture = texture->texture;
    wgpu::TexelCopyBufferLayout layout{};
    layout.bytesPerRow = static_cast<std::uint32_t>(bytesPerRow);
    layout.rowsPerImage = texture->desc.height;
    const wgpu::Extent3D size{texture->desc.width, texture->desc.height, 1};
    // Row 0 is texture coordinate v = 0 in both APIs, so GL-ordered rows go in unchanged.
    _device->Queue().WriteTexture(&destination, data, bytesPerRow * texture->desc.height, &layout, &size);
}

ObjectId CommandList::CreateUniformBuffer(std::size_t size) {
    wgpu::BufferDescriptor descriptor{};
    descriptor.size = AlignUp(static_cast<std::uint32_t>(size), 16);
    descriptor.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
    _buffers.push_back(_device->Get().CreateBuffer(&descriptor));
    return static_cast<ObjectId>(_buffers.size());
}

ObjectId CommandList::CreateVertexBuffer(const void* data, std::size_t size) {
    wgpu::BufferDescriptor descriptor{};
    descriptor.size = AlignUp(static_cast<std::uint32_t>(size), 4);
    descriptor.usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst;
    _buffers.push_back(_device->Get().CreateBuffer(&descriptor));
    const auto id = static_cast<ObjectId>(_buffers.size());
    WriteBuffer(id, data, size);
    return id;
}

void CommandList::WriteBuffer(ObjectId id, const void* data, std::size_t size) {
    if (id == 0 || id > _buffers.size()) {
        return;
    }
    std::vector<std::uint8_t> padded(static_cast<const std::uint8_t*>(data),
                                     static_cast<const std::uint8_t*>(data) + size);
    padded.resize(AlignUp(static_cast<std::uint32_t>(size), 4), 0); // writeBuffer takes multiples of 4
    _device->Queue().WriteBuffer(_buffers[id - 1], 0, padded.data(), padded.size());
}

FramebufferId CommandList::CreateFramebuffer(ObjectId color, ObjectId depth) {
    _framebuffers.push_back(Framebuffer{color, depth});
    return static_cast<FramebufferId>(_framebuffers.size());
}

void CommandList::ResizeBackbuffer(std::uint32_t width, std::uint32_t height) {
    const auto replace = [&](ObjectId& id, TextureFormat format) {
        TextureDesc desc;
        desc.width = width;
        desc.height = height;
        desc.format = format;
        if (id != 0) {
            _textures[id - 1].texture.Destroy();
            wgpu::TextureDescriptor descriptor{};
            descriptor.size = {width, height, 1};
            descriptor.format = ToWgpu(format);
            descriptor.usage =
                wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopySrc | wgpu::TextureUsage::RenderAttachment;
            _textures[id - 1].texture = _device->Get().CreateTexture(&descriptor);
            _textures[id - 1].view = _textures[id - 1].texture.CreateView();
            _textures[id - 1].desc = desc;
        } else {
            id = CreateTexture(desc, Filter::Nearest);
        }
    };
    replace(_backbufferColor, TextureFormat::Rgba8);
    replace(_backbufferDepth, TextureFormat::Depth24);
}

const CommandList::Texture* CommandList::FindTexture(ObjectId id) const {
    return id == 0 || id > _textures.size() ? nullptr : &_textures[id - 1];
}

void CommandList::SetProfiling(FrameProfile::PassTimings* passes, FrameProfile::CpuStages* stages) {
    _passTimings = passes;
    _cpuStages = stages;
}

void CommandList::BeginFrame() {
    _encoder = _device->Get().CreateCommandEncoder();
    _ring.used = 0;
    _lastStaged = nullptr;
    _stats.uniformBytes = 0;
    if (_timestamps.querySet) {
        _timestamps.pairs[_timestamps.current].BeginFrame();
    }
}

void CommandList::EndFrame() {
    _planner.EndGraphPass();
    if (!_encoder) {
        return;
    }
    if (_ring.used > 0) {
        _device->Queue().WriteBuffer(_ring.buffer, 0, _ring.staging.data(),
                                     AlignUp(static_cast<std::uint32_t>(_ring.used), 4));
    }
    _stats.uniformBytes = _ring.used;
    const std::size_t slot = _timestamps.current;
    const std::uint32_t pairs = _timestamps.querySet ? _timestamps.pairs[slot].Count() : 0;
    const bool timed = pairs > 0 && !_timestamps.busy[slot];
    if (timed) {
        const std::uint64_t bytes = 2ull * pairs * sizeof(std::uint64_t);
        _encoder.ResolveQuerySet(_timestamps.querySet, 0, 2 * pairs, _timestamps.resolve, 0);
        _encoder.CopyBufferToBuffer(_timestamps.resolve, 0, _timestamps.readback[slot], 0, bytes);
    }
    const wgpu::CommandBuffer commands = _encoder.Finish();
    _encoder = nullptr;
    _device->Queue().Submit(1, &commands);
    if (timed) {
        _timestamps.busy[slot] = true;
        const std::uint64_t bytes = 2ull * pairs * sizeof(std::uint64_t);
        _timestamps.readback[slot].MapAsync(
            wgpu::MapMode::Read, 0, bytes, wgpu::CallbackMode::AllowSpontaneous,
            [this, slot, bytes](wgpu::MapAsyncStatus status, wgpu::StringView) {
                if (status == wgpu::MapAsyncStatus::Success) {
                    const auto* ticks =
                        static_cast<const std::uint64_t*>(_timestamps.readback[slot].GetConstMappedRange(0, bytes));
                    std::fill(_gpuMs.begin(), _gpuMs.end(), 0.0);
                    _timestamps.pairs[slot].Resolve(ticks, bytes / sizeof(std::uint64_t), _gpuMs);
                    _gpuValid = true;
                    _timestamps.readback[slot].Unmap();
                }
                _timestamps.busy[slot] = false;
            });
    }
    _timestamps.current = 1 - _timestamps.current;
    _device->Poll();
}

void CommandList::BeginPass(const PassInfo& pass) {
    _open = pass;
    _inPass = true;
    _planner.BeginGraphPass(pass.timerSlot >= 0 ? static_cast<int>(FrameProfile::kStageCount) + pass.timerSlot : -1);
    _started = FrameProfile::CpuStages::Clock::now();
}

void CommandList::EndPass() {
    if (!_inPass) {
        return;
    }
    _planner.EndGraphPass();
    const auto elapsed = FrameProfile::CpuStages::Clock::now() - _started;
    if (_passTimings) {
        _passTimings->Add(_open.timerSlot, elapsed);
    }
    if (_cpuStages && _open.stage >= 0 && _open.stage < static_cast<int>(FrameProfile::kStageCount)) {
        _cpuStages->Add(static_cast<FrameProfile::Stage>(_open.stage), elapsed);
    }
    _inPass = false;
}

void CommandList::SetRenderTarget(const RenderTarget& target) {
    _planner.BindFramebuffer(target.framebuffer);
    _planner.SetViewport({target.x, target.y, target.width, target.height});
}

void CommandList::BindFramebuffer(FramebufferId framebuffer) {
    _planner.BindFramebuffer(framebuffer);
}

void CommandList::SetViewport(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height) {
    _planner.SetViewport({x, y, width, height});
}

void CommandList::Clear(ClearFlags flags) {
    _planner.Clear(flags, _clearValue, _tracker.Current().depthWrite);
}

void CommandList::SetPipelineState(const PipelineState& state) {
    _tracker.Apply(state);
}

void CommandList::SetBindGroup(const BindGroup& group) {
    for (std::size_t i = 0; i < group.textureCount; ++i) {
        if (group.textures[i].unit < _unitTextures.size()) {
            _unitTextures[group.textures[i].unit] = group.textures[i].texture;
        }
    }
    for (std::size_t i = 0; i < group.uniformBufferCount; ++i) {
        if (group.uniformBuffers[i].binding < _uniformBuffers.size()) {
            _uniformBuffers[group.uniformBuffers[i].binding] = group.uniformBuffers[i].buffer;
        }
    }
}

void CommandList::SetVertexBuffer(ObjectId buffer, const VertexLayout& layout) {
    _vertexBuffer = buffer;
    _vertexLayout = layout;
    _vertexLayoutId = 0;
    if (buffer == 0) {
        return;
    }
    const auto known = std::find(_layouts.begin(), _layouts.end(), layout);
    if (known == _layouts.end()) {
        _layouts.push_back(layout);
        _vertexLayoutId = static_cast<std::uint32_t>(_layouts.size());
    } else {
        _vertexLayoutId = static_cast<std::uint32_t>(known - _layouts.begin()) + 1;
    }
}

void CommandList::BeginRenderPass(const RenderPassBegin& begin) {
    if (!_encoder) {
        BeginFrame(); // a pass recorded outside BeginFrame/EndFrame still lands in a frame
    }
    const Framebuffer target = begin.framebuffer == 0 ? Framebuffer{_backbufferColor, _backbufferDepth}
                               : begin.framebuffer <= _framebuffers.size() ? _framebuffers[begin.framebuffer - 1]
                                                                           : Framebuffer{};
    const Texture* color = FindTexture(target.color);
    const Texture* depth = FindTexture(target.depth);
    wgpu::RenderPassColorAttachment colorAttachment{};
    wgpu::RenderPassDepthStencilAttachment depthAttachment{};
    wgpu::RenderPassDescriptor descriptor{};
    if (color) {
        colorAttachment.view = color->view;
        colorAttachment.loadOp = begin.colorLoad == LoadOp::Clear ? wgpu::LoadOp::Clear : wgpu::LoadOp::Load;
        colorAttachment.storeOp = wgpu::StoreOp::Store;
        colorAttachment.clearValue = {begin.clear.color[0], begin.clear.color[1], begin.clear.color[2],
                                      begin.clear.color[3]};
        descriptor.colorAttachmentCount = 1;
        descriptor.colorAttachments = &colorAttachment;
    }
    if (depth) {
        depthAttachment.view = depth->view;
        depthAttachment.depthLoadOp = begin.depthLoad == LoadOp::Clear ? wgpu::LoadOp::Clear : wgpu::LoadOp::Load;
        depthAttachment.depthStoreOp = wgpu::StoreOp::Store;
        depthAttachment.depthClearValue = begin.clear.depth;
        descriptor.depthStencilAttachment = &depthAttachment;
    }
    wgpu::PassTimestampWrites timestamps{};
    if (_timestamps.querySet && !_timestamps.busy[_timestamps.current]) {
        const int pair = _timestamps.pairs[_timestamps.current].Reserve(begin.timerSlot);
        if (pair >= 0) {
            timestamps.querySet = _timestamps.querySet;
            timestamps.beginningOfPassWriteIndex = 2 * static_cast<std::uint32_t>(pair);
            timestamps.endOfPassWriteIndex = 2 * static_cast<std::uint32_t>(pair) + 1;
            descriptor.timestampWrites = &timestamps;
        }
    }
    descriptor.label = _inPass ? _open.name : "pass";
    _pass = _encoder.BeginRenderPass(&descriptor);
    ++_stats.renderPasses;
    ApplyViewport(begin.viewport);
}

void CommandList::SetPassViewport(const Viewport& viewport) {
    ApplyViewport(viewport);
}

void CommandList::ApplyViewport(const Viewport& viewport) {
    if (!_pass) {
        return;
    }
    // GL clips a viewport hanging off the target; WebGPU rejects one, so clamp to the attachment.
    const FramebufferId id = _planner.Framebuffer();
    const Framebuffer target = id == 0                      ? Framebuffer{_backbufferColor, _backbufferDepth}
                               : id <= _framebuffers.size() ? _framebuffers[id - 1]
                                                            : Framebuffer{};
    const Texture* attachment = FindTexture(target.color ? target.color : target.depth);
    if (!attachment) {
        return;
    }
    const auto width = static_cast<float>(attachment->desc.width);
    const auto height = static_cast<float>(attachment->desc.height);
    // Rendering is y-flipped (GL row order in memory), so GL's bottom-left origin is the right
    // one here too: no y = height - y - h.
    const float x = std::clamp(static_cast<float>(viewport.x), 0.0f, width);
    const float y = std::clamp(static_cast<float>(viewport.y), 0.0f, height);
    const float w = viewport.width > 0 ? std::min(static_cast<float>(viewport.width), width - x) : width - x;
    const float h = viewport.height > 0 ? std::min(static_cast<float>(viewport.height), height - y) : height - y;
    _pass.SetViewport(x, y, std::max(w, 0.0f), std::max(h, 0.0f), 0.0f, 1.0f);
}

void CommandList::EndRenderPass() {
    if (_pass) {
        _pass.End();
        _pass = nullptr;
    }
}

wgpu::RenderPipeline CommandList::PipelineFor(PrimitiveTopology topology) {
    const FramebufferId id = _planner.Framebuffer();
    const Framebuffer target = id == 0                      ? Framebuffer{_backbufferColor, _backbufferDepth}
                               : id <= _framebuffers.size() ? _framebuffers[id - 1]
                                                            : Framebuffer{};
    const Texture* color = FindTexture(target.color);
    const Texture* depth = FindTexture(target.depth);

    PipelineKey key;
    key.program = _program->Id();
    key.vertexLayout = _vertexLayoutId;
    key.topology = topology;
    key.colorFormat = color ? color->desc.format : TextureFormat::Unknown;
    key.depthFormat = depth ? depth->desc.format : TextureFormat::Unknown;
    key.fixedFunction = ToWebGpuFixedFunction(_tracker.Current(), /*flipY=*/true);
    if (const auto found = _pipelines.find(key); found != _pipelines.end()) {
        return found->second;
    }

    const WebGpuFixedFunction& ff = key.fixedFunction;
    std::vector<wgpu::VertexAttribute> attributes;
    wgpu::VertexBufferLayout bufferLayout{};
    if (_vertexLayoutId != 0) {
        for (std::size_t i = 0; i < _vertexLayout.attributeCount; ++i) {
            const VertexAttribute& a = _vertexLayout.attributes[i];
            // As given: WebGPU accepts attributes the shader does not read, as GL does.
            wgpu::VertexAttribute attribute{};
            attribute.format = FloatFormat(a.components);
            attribute.offset = a.offset;
            attribute.shaderLocation = a.location;
            attributes.push_back(attribute);
        }
        bufferLayout.arrayStride = _vertexLayout.stride;
        bufferLayout.stepMode = wgpu::VertexStepMode::Vertex;
        bufferLayout.attributeCount = attributes.size();
        bufferLayout.attributes = attributes.data();
    }

    wgpu::RenderPipelineDescriptor descriptor{};
    descriptor.label = _program->Label().c_str();
    descriptor.layout = _program->Layout();
    descriptor.vertex.module = _program->Module(StageKind::Vertex);
    descriptor.vertex.entryPoint = "main";
    descriptor.vertex.bufferCount = _vertexLayoutId != 0 ? 1 : 0;
    descriptor.vertex.buffers = _vertexLayoutId != 0 ? &bufferLayout : nullptr;
    descriptor.primitive.topology = ToWgpu(topology);
    descriptor.primitive.frontFace =
        ff.frontFace == FrontFace::CounterClockwise ? wgpu::FrontFace::CCW : wgpu::FrontFace::CW;
    descriptor.primitive.cullMode = ff.cullBack ? wgpu::CullMode::Back : wgpu::CullMode::None;

    wgpu::DepthStencilState depthState{};
    if (depth) {
        depthState.format = ToWgpu(depth->desc.format);
        depthState.depthWriteEnabled = ff.depthWrite ? wgpu::OptionalBool::True : wgpu::OptionalBool::False;
        depthState.depthCompare = ToWgpu(ff.depthCompare);
        const bool lines = topology == PrimitiveTopology::Lines || topology == PrimitiveTopology::LineStrip ||
                           topology == PrimitiveTopology::Points;
        depthState.depthBias = lines ? 0 : ff.depthBias; // WebGPU: bias only on triangles
        depthState.depthBiasSlopeScale = lines ? 0.0f : ff.depthBiasSlopeScale;
        descriptor.depthStencil = &depthState;
    }

    wgpu::BlendState blend{};
    blend.color = {wgpu::BlendOperation::Add, ToWgpu(ff.srcFactor), ToWgpu(ff.dstFactor)};
    blend.alpha = blend.color; // glBlendFunc sets both
    wgpu::ColorTargetState colorTarget{};
    wgpu::FragmentState fragment{};
    if (color && !ff.discard) {
        colorTarget.format = ToWgpu(color->desc.format);
        colorTarget.blend = ff.blend ? &blend : nullptr;
        fragment.module = _program->Module(StageKind::Fragment);
        fragment.entryPoint = "main";
        fragment.targetCount = 1;
        fragment.targets = &colorTarget;
        descriptor.fragment = &fragment;
    } else if (depth && !ff.discard) {
        // Depth-only target (the shadow map): the fragment stage still runs, for discard and
        // gl_FragDepth, with no colour target.
        fragment.module = _program->Module(StageKind::Fragment);
        fragment.entryPoint = "main";
        descriptor.fragment = &fragment;
    }

    wgpu::RenderPipeline pipeline = _device->Get().CreateRenderPipeline(&descriptor);
    ++_stats.pipelinesCreated;
    _pipelines.emplace(key, pipeline);
    return pipeline;
}

bool CommandList::StageUniforms(std::array<std::uint32_t, 2>& offsets) {
    const bool dirty = _program->TakeDirty();
    if (!dirty && _lastStaged == _program) {
        offsets = _lastOffsets; // nothing changed since the last draw of this program
        return true;
    }
    const std::uint32_t alignment = _device->UniformOffsetAlignment();
    for (std::size_t stage = 0; stage < 2; ++stage) {
        const std::vector<std::uint8_t>& block = _program->Block(stage == 0 ? StageKind::Vertex : StageKind::Fragment);
        const std::uint32_t start = AlignUp(static_cast<std::uint32_t>(_ring.used), alignment);
        const std::size_t size = std::max<std::size_t>(block.size(), 16);
        if (start + size > _ring.staging.size()) {
            LOG_ERROR("WebGPU", "uniform ring full (%zu bytes this frame): draw skipped", _ring.used);
            return false;
        }
        if (!block.empty()) {
            std::memcpy(_ring.staging.data() + start, block.data(), block.size());
        }
        offsets[stage] = start;
        _ring.used = start + size;
    }
    _lastStaged = _program;
    _lastOffsets = offsets;
    return true;
}

bool CommandList::BindForDraw() {
    std::array<std::uint32_t, 2> offsets{};
    if (!StageUniforms(offsets)) {
        return false;
    }
    const wgpu::Device& device = _device->Get();
    // Group 0: the per-view uniform blocks, at their GL binding points.
    std::vector<wgpu::BindGroupEntry> frame;
    for (const ManifestBinding& binding : _program->FrameBindings()) {
        const ObjectId id = binding.binding < _uniformBuffers.size() ? _uniformBuffers[binding.binding] : 0;
        if (id == 0 || id > _buffers.size()) {
            LOG_WARN("WebGPU", "%s: no buffer bound at uniform binding %u", _program->Label().c_str(), binding.binding);
            return false;
        }
        wgpu::BindGroupEntry entry{};
        entry.binding = binding.binding;
        entry.buffer = _buffers[id - 1];
        entry.size = _buffers[id - 1].GetSize();
        frame.push_back(entry);
    }
    // Group 1: each sampled unit's texture and sampler (gfx/rhi/WgslBindings.h numbering).
    std::vector<wgpu::BindGroupEntry> textures;
    for (const int unit : _program->TextureUnits()) {
        const Texture* texture = FindTexture(_unitTextures[static_cast<std::size_t>(unit)]);
        if (!texture) {
            LOG_WARN("WebGPU", "%s: nothing bound on texture unit %d", _program->Label().c_str(), unit);
            return false;
        }
        wgpu::BindGroupEntry image{};
        image.binding = rhi::wgsl::TextureBinding(static_cast<std::uint32_t>(unit));
        image.textureView = texture->view;
        wgpu::BindGroupEntry sampler{};
        sampler.binding = rhi::wgsl::SamplerBinding(static_cast<std::uint32_t>(unit));
        sampler.sampler = texture->sampler;
        textures.push_back(image);
        textures.push_back(sampler);
    }
    // Group 2: both stages' uniform blocks in this frame's ring, at dynamic offsets.
    std::vector<wgpu::BindGroupEntry> uniforms;
    std::vector<std::uint32_t> dynamicOffsets;
    for (std::size_t stage = 0; stage < 2; ++stage) {
        const std::vector<std::uint8_t>& block = _program->Block(stage == 0 ? StageKind::Vertex : StageKind::Fragment);
        if (block.empty()) {
            continue;
        }
        wgpu::BindGroupEntry entry{};
        entry.binding = stage == 0 ? rhi::wgsl::kVertexUniformBinding : rhi::wgsl::kFragmentUniformBinding;
        entry.buffer = _ring.buffer;
        entry.size = AlignUp(static_cast<std::uint32_t>(block.size()), 16);
        uniforms.push_back(entry);
        dynamicOffsets.push_back(offsets[stage]);
    }
    const std::array<const std::vector<wgpu::BindGroupEntry>*, 3> groups{&frame, &textures, &uniforms};
    for (std::uint32_t group = 0; group < 3; ++group) {
        wgpu::BindGroupDescriptor descriptor{};
        descriptor.layout = _program->GroupLayout(group);
        descriptor.entryCount = groups[group]->size();
        descriptor.entries = groups[group]->data();
        const wgpu::BindGroup bindGroup = device.CreateBindGroup(&descriptor);
        if (group == rhi::wgsl::kUniformGroup) {
            _pass.SetBindGroup(group, bindGroup, dynamicOffsets.size(), dynamicOffsets.data());
        } else {
            _pass.SetBindGroup(group, bindGroup);
        }
    }
    return true;
}

void CommandList::Draw(std::uint32_t vertexCount, PrimitiveTopology topology, std::uint32_t instanceCount) {
    if (!_program) {
        LOG_WARN("WebGPU", "draw with no program");
        return;
    }
    _planner.BeforeDraw();
    if (!_pass) {
        return;
    }
    _pass.SetPipeline(PipelineFor(topology));
    if (!BindForDraw()) {
        return;
    }
    if (_vertexLayoutId != 0 && _vertexBuffer != 0 && _vertexBuffer <= _buffers.size()) {
        _pass.SetVertexBuffer(0, _buffers[_vertexBuffer - 1]);
    }
    _pass.Draw(vertexCount, instanceCount);
    ++_stats.draws;
}

void CommandList::DrawFullscreenTriangle(ObjectId /*emptyVertexArray*/) {
    const ObjectId buffer = _vertexBuffer;
    const VertexLayout layout = _vertexLayout;
    SetVertexBuffer(0, {});
    Draw(3);
    SetVertexBuffer(buffer, layout);
}

void CommandList::Present(const wgpu::TextureView& target, wgpu::TextureFormat format) {
    const Texture* source = FindTexture(_backbufferColor);
    if (!source) {
        return;
    }
    _planner.EndGraphPass();
    if (!_encoder) {
        BeginFrame();
    }
    wgpu::RenderPipeline& pipeline = _blitPipelines[static_cast<std::uint32_t>(format)];
    if (!pipeline) {
        wgpu::PipelineLayoutDescriptor layout{};
        layout.bindGroupLayoutCount = 1;
        layout.bindGroupLayouts = &_blitLayout;
        wgpu::ColorTargetState colorTarget{};
        colorTarget.format = format;
        wgpu::FragmentState fragment{};
        fragment.module = _blitModule;
        fragment.entryPoint = "fs";
        fragment.targetCount = 1;
        fragment.targets = &colorTarget;
        wgpu::RenderPipelineDescriptor descriptor{};
        descriptor.label = "present blit";
        descriptor.layout = _device->Get().CreatePipelineLayout(&layout);
        descriptor.vertex.module = _blitModule;
        descriptor.vertex.entryPoint = "vs";
        descriptor.fragment = &fragment;
        pipeline = _device->Get().CreateRenderPipeline(&descriptor);
    }
    wgpu::BindGroupEntry entry{};
    entry.binding = 0;
    entry.textureView = source->view;
    wgpu::BindGroupDescriptor group{};
    group.layout = _blitLayout;
    group.entryCount = 1;
    group.entries = &entry;
    const wgpu::BindGroup bindGroup = _device->Get().CreateBindGroup(&group);

    wgpu::RenderPassColorAttachment attachment{};
    attachment.view = target;
    attachment.loadOp = wgpu::LoadOp::Clear;
    attachment.storeOp = wgpu::StoreOp::Store;
    attachment.clearValue = {0.0, 0.0, 0.0, 1.0};
    wgpu::RenderPassDescriptor pass{};
    pass.label = "present";
    pass.colorAttachmentCount = 1;
    pass.colorAttachments = &attachment;
    const wgpu::RenderPassEncoder encoder = _encoder.BeginRenderPass(&pass);
    encoder.SetPipeline(pipeline);
    encoder.SetBindGroup(0, bindGroup);
    encoder.Draw(3);
    encoder.End();
}

void CommandList::PresentInto(ObjectId texture) {
    if (const Texture* target = FindTexture(texture)) {
        Present(target->view, ToWgpu(target->desc.format));
    }
}

std::vector<std::uint8_t> CommandList::ReadColor(FramebufferId framebuffer) {
#ifdef __EMSCRIPTEN__
    (void)framebuffer;
    return {};
#else
    const Framebuffer target = framebuffer == 0                      ? Framebuffer{_backbufferColor, _backbufferDepth}
                               : framebuffer <= _framebuffers.size() ? _framebuffers[framebuffer - 1]
                                                                     : Framebuffer{};
    const Texture* color = FindTexture(target.color);
    if (!color || BytesPerTexel(color->desc.format) == 0) {
        return {};
    }
    const std::uint32_t width = color->desc.width;
    const std::uint32_t height = color->desc.height;
    const std::uint32_t texel = BytesPerTexel(color->desc.format);
    const std::uint32_t paddedRow = AlignUp(width * texel, 256); // copyTextureToBuffer's row alignment
    wgpu::BufferDescriptor bufferDescriptor{};
    bufferDescriptor.size = static_cast<std::uint64_t>(paddedRow) * height;
    bufferDescriptor.usage = wgpu::BufferUsage::MapRead | wgpu::BufferUsage::CopyDst;
    const wgpu::Buffer buffer = _device->Get().CreateBuffer(&bufferDescriptor);

    const wgpu::CommandEncoder encoder = _device->Get().CreateCommandEncoder();
    wgpu::TexelCopyTextureInfo source{};
    source.texture = color->texture;
    wgpu::TexelCopyBufferInfo destination{};
    destination.buffer = buffer;
    destination.layout.bytesPerRow = paddedRow;
    destination.layout.rowsPerImage = height;
    const wgpu::Extent3D size{width, height, 1};
    encoder.CopyTextureToBuffer(&source, &destination, &size);
    const wgpu::CommandBuffer commands = encoder.Finish();
    _device->Queue().Submit(1, &commands);

    bool mapped = false;
    const wgpu::Future future = buffer.MapAsync(
        wgpu::MapMode::Read, 0, bufferDescriptor.size, wgpu::CallbackMode::WaitAnyOnly,
        [&](wgpu::MapAsyncStatus status, wgpu::StringView) { mapped = status == wgpu::MapAsyncStatus::Success; });
    if (!_device->Wait(future) || !mapped) {
        return {};
    }
    const auto* bytes = static_cast<const std::uint8_t*>(buffer.GetConstMappedRange(0, bufferDescriptor.size));
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * texel);
    for (std::uint32_t row = 0; row < height; ++row) {
        std::memcpy(pixels.data() + static_cast<std::size_t>(row) * width * texel,
                    bytes + static_cast<std::size_t>(row) * paddedRow, static_cast<std::size_t>(width) * texel);
    }
    buffer.Unmap();
    return pixels;
#endif
}

} // namespace rhi::webgpu
