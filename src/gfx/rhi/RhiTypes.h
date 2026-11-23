#ifndef SOLARSYSTEM_GFX_RHI_RHITYPES_H
#define SOLARSYSTEM_GFX_RHI_RHITYPES_H
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

/**
 * The thin render-hardware interface's value types (docs/ARCHITECTURE.md §7.5). GL-free, so the
 * frame graph (render/graph/) and its unit tests use them without a context; the GL backend
 * (GlCommandList) maps them onto the existing gfx/ objects. A WebGPU backend would map the
 * same descriptions onto its own pipelines and bind groups.
 */
namespace rhi {

enum class TextureFormat : std::uint8_t {
    Unknown,
    Rgba8,    // the default framebuffer
    Rgba16F,  // HDR and bloom targets
    Depth24,  // renderbuffer depth beside a colour target
    Depth32F, // the shadow map
    Buffer,   // not a texture: a GPU buffer the graph tracks (particle state)
};

/** Size and format of a render target or tracked buffer; two transients with equal descs may share memory. */
struct TextureDesc {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    TextureFormat format = TextureFormat::Unknown;
    /** Fraction of the display it follows (bloom is half-resolution), 0 = fixed size. */
    float displayScale = 0.0f;

    bool operator==(const TextureDesc& other) const {
        return width == other.width && height == other.height && format == other.format &&
               displayScale == other.displayScale;
    }
    bool operator!=(const TextureDesc& other) const { return !(*this == other); }
};

enum class BlendFactor : std::uint8_t {
    Zero,
    One,
    SrcAlpha,
    OneMinusSrcAlpha,
    OneMinusSrcColor,
};

enum class CompareOp : std::uint8_t { Less, LessEqual, Always };
enum class FrontFace : std::uint8_t { CounterClockwise, Clockwise };

struct BlendFunc {
    BlendFactor src = BlendFactor::One;
    BlendFactor dst = BlendFactor::Zero;
    bool operator==(const BlendFunc& other) const { return src == other.src && dst == other.dst; }
};

/**
 * Fixed-function state a pass changes. Every field is optional: an unset field leaves that
 * state as the previous pass left it, which is exactly what the hand-written glEnable/glBlendFunc
 * sequences did, so moving a pass onto the RHI cannot change a pixel. A backend with whole
 * pipeline objects folds these deltas into the state it tracks.
 */
struct PipelineState {
    std::optional<bool> blend;
    std::optional<BlendFunc> blendFunc;
    std::optional<bool> depthTest;
    std::optional<bool> depthWrite;
    std::optional<CompareOp> depthFunc;
    std::optional<bool> cullFace;
    std::optional<FrontFace> frontFace;
    std::optional<bool> polygonOffset;
    float polygonOffsetFactor = 0.0f;
    float polygonOffsetUnits = 0.0f;
    std::optional<bool> rasterizerDiscard;
    std::optional<bool> programPointSize; // native only: always on in GLES 3 / WebGL 2

    // Chainable: rhi::PipelineState().SetBlend(true).SetDepthWrite(false).
    PipelineState& SetBlend(bool enabled) {
        blend = enabled;
        return *this;
    }
    PipelineState& SetBlendFunc(BlendFactor src, BlendFactor dst) {
        blendFunc = BlendFunc{src, dst};
        return *this;
    }
    PipelineState& SetDepthTest(bool enabled) {
        depthTest = enabled;
        return *this;
    }
    PipelineState& SetDepthWrite(bool enabled) {
        depthWrite = enabled;
        return *this;
    }
    PipelineState& SetDepthFunc(CompareOp op) {
        depthFunc = op;
        return *this;
    }
    PipelineState& SetCullFace(bool enabled) {
        cullFace = enabled;
        return *this;
    }
    PipelineState& SetFrontFace(FrontFace face) {
        frontFace = face;
        return *this;
    }
    PipelineState& SetPolygonOffset(bool enabled, float factor = 0.0f, float units = 0.0f) {
        polygonOffset = enabled;
        polygonOffsetFactor = factor;
        polygonOffsetUnits = units;
        return *this;
    }
    PipelineState& SetProgramPointSize(bool enabled) {
        programPointSize = enabled;
        return *this;
    }
};

/** GL name (or backend id) of a framebuffer; 0 is the window's (or the XR layer, see CommandList). */
using FramebufferId = std::uint32_t;
/** GL name (or backend id) of a texture or buffer. */
using ObjectId = std::uint32_t;

enum class ClearFlags : std::uint8_t { None = 0, Color = 1, Depth = 2, ColorDepth = 3 };

/** A render target: which framebuffer, and the viewport inside it. */
struct RenderTarget {
    FramebufferId framebuffer = 0;
    std::int32_t x = 0, y = 0, width = 0, height = 0;
};

/** One texture bound to a unit (gfx/TextureUnits.h numbering). */
struct TextureBinding {
    std::uint32_t unit = 0;
    ObjectId texture = 0;
};
/** One uniform buffer bound to an indexed binding point (FrameUBO 0, MagnetosphereUBO 1). */
struct UniformBufferBinding {
    std::uint32_t binding = 0;
    ObjectId buffer = 0;
};

/**
 * What a draw reads besides its program: textures on units and uniform buffers on bindings.
 * Fixed capacity, so a pass can build one per draw without allocating.
 */
struct BindGroup {
    static constexpr std::size_t kMaxTextures = 8;
    static constexpr std::size_t kMaxUniformBuffers = 4;
    std::array<TextureBinding, kMaxTextures> textures{};
    std::array<UniformBufferBinding, kMaxUniformBuffers> uniformBuffers{};
    std::uint8_t textureCount = 0;
    std::uint8_t uniformBufferCount = 0;

    BindGroup& Texture(std::uint32_t unit, ObjectId texture) {
        if (textureCount < kMaxTextures) {
            textures[textureCount++] = TextureBinding{unit, texture};
        }
        return *this;
    }
    BindGroup& UniformBuffer(std::uint32_t binding, ObjectId buffer) {
        if (uniformBufferCount < kMaxUniformBuffers) {
            uniformBuffers[uniformBufferCount++] = UniformBufferBinding{binding, buffer};
        }
        return *this;
    }
};

/** A pass as the command list sees it: names for debuggers and the timer slot it is billed to. */
struct PassInfo {
    const char* name = ""; // unique in the frame: "scene:Earth/0"
    const char* kind = ""; // what the timers sum over: "scene"
    int timerSlot = -1;    // PassTimings slot of `kind`, -1 untimed
    int stage = -1;        // FrameProfile::Stage the time also counts towards
};

} // namespace rhi

#endif // SOLARSYSTEM_GFX_RHI_RHITYPES_H
