#ifndef SOLARSYSTEM_GFX_FRAMEUNIFORMS_H
#define SOLARSYSTEM_GFX_FRAMEUNIFORMS_H
#include "GlHandle.h"
#include <glm/glm.hpp>
#include <array>
#include <cstddef>

/**
 * CPU mirror of `FrameUBO` (resource/shaders/common/frame.glsl), std140. Every member is
 * either a mat4/vec4 or one of four floats packed into the final vec4 slot, so the C++
 * layout is the std140 layout with no manual padding; the static_asserts pin it.
 */
struct FrameUniformData {
    glm::mat4 view{1.0f};
    glm::mat4 proj{1.0f};
    glm::mat4 viewProj{1.0f};
    glm::vec4 cameraPos{0.0f};
    glm::vec4 sunPos{0.0f};
    float farPlane = 0.0f;
    float zCoef = 0.0f;
    float time = 0.0f;
    float exposure = 0.0f;
};
static_assert(offsetof(FrameUniformData, proj) == 64, "std140: mat4 is 64 bytes");
static_assert(offsetof(FrameUniformData, cameraPos) == 192, "std140: FrameUBO.cameraPos");
static_assert(offsetof(FrameUniformData, farPlane) == 224, "std140: FrameUBO.farPlane");
static_assert(sizeof(FrameUniformData) == 240, "std140: FrameUBO size");

/**
 * One uniform buffer per view (mono, or each XR eye): both eyes are uploaded once per frame
 * and BeginView only rebinds, since XR interleaves the eyes per scene component.
 */
class FrameUniformBuffer {
public:
    static constexpr unsigned int kBinding = 0; // FrameUBO's uniform-buffer binding point
    static constexpr std::size_t kMaxViews = 2;

    void Upload(std::size_t view, const FrameUniformData& data);
    void Bind(std::size_t view) const;

private:
    std::array<GlBuffer, kMaxViews> _buffers;
};

#endif // SOLARSYSTEM_GFX_FRAMEUNIFORMS_H
