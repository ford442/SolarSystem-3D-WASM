#include "XrPointerRenderer.h"

#include <algorithm>
#include <cstddef>

namespace {

constexpr float kRayLength = 40.0f;
constexpr glm::vec3 kHandColor[2] = {
    {0.35f, 0.85f, 1.0f},
    {1.0f, 0.55f, 0.25f},
};

} // namespace

XrPointerRenderer::XrPointerRenderer() {
    _shader = std::make_unique<Shader>("resource/shaders/missionPath.vs", "resource/shaders/missionPath.fs");
}

void XrPointerRenderer::SetRay(int hand, const glm::vec3& origin, const glm::vec3& direction, bool visible) {
    const int idx = std::clamp(hand, 0, 1);
    _rays[idx].origin = origin;
    const float len = glm::length(direction);
    _rays[idx].direction = len > 1.0e-5f ? direction / len : glm::vec3(0.0f, 0.0f, -1.0f);
    _rays[idx].visible = visible;
}

void XrPointerRenderer::Clear() {
    _rays[0].visible = false;
    _rays[1].visible = false;
}

void XrPointerRenderer::Draw(const glm::mat4& projection, const glm::mat4& view, const glm::vec3& cameraPos,
                             float zCoef) const {
    if (!_shader || !AnyVisible()) {
        return;
    }
    _shader->Use();
    _shader->SetMat4("projection", projection);
    _shader->SetMat4("view", view);
    _shader->SetVec3("cameraPos", cameraPos);
    _shader->SetFloat("ribbonWidth", 0.045f);
    _shader->SetFloat("zCoef", zCoef);
    _shader->SetFloat("uBaseOpacity", 0.9f);
    _shader->SetInt("uFocused", 1);

    for (int hand = 0; hand < 2; ++hand) {
        if (!_rays[hand].visible) {
            continue;
        }
        const glm::vec3 a = _rays[hand].origin;
        const glm::vec3 b = a + _rays[hand].direction * kRayLength;
        const glm::vec3 tangent = _rays[hand].direction;
        std::vector<RibbonVertex> verts;
        LineStripBatch::AppendSegment(verts, a, b, tangent, 0.0f, kRayLength);
        _ray.Stream(verts);
        _shader->SetVec3("lineColor", kHandColor[hand]);
        _ray.Draw();
    }
}
