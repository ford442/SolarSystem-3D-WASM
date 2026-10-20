#include "MissionPathRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace {

constexpr float kProbeSize = 2.4f;

} // namespace

MissionPathRenderer::MissionPathRenderer() {
    _shader = std::make_unique<Shader>("resource/shaders/missionPath.vs", "resource/shaders/missionPath.fs");
}

void MissionPathRenderer::Clear() {
    _path.Clear();
}

std::vector<RibbonVertex> MissionPathRenderer::Expand(
    const std::vector<glm::vec3>& positions) {
    std::vector<RibbonVertex> verts;
    if (positions.size() < 2) {
        return verts;
    }
    float arc = 0.0f;
    verts.reserve((positions.size() - 1) * 6);
    for (size_t i = 0; i + 1 < positions.size(); ++i) {
        const glm::vec3& a = positions[i];
        const glm::vec3& b = positions[i + 1];
        glm::vec3 tangent = b - a;
        const float tlen = glm::length(tangent);
        if (tlen < 1.0e-6f) {
            continue;
        }
        tangent /= tlen;
        const float nextArc = arc + tlen;
        LineStripBatch::AppendSegment(verts, a, b, tangent, arc, nextArc);
        arc = nextArc;
    }
    return verts;
}

void MissionPathRenderer::Upload(const std::vector<glm::vec3>& scenePositions) {
    _path.Upload(Expand(scenePositions));
}

void MissionPathRenderer::Draw(const glm::mat4& projection, const glm::mat4& view, const glm::vec3& cameraPos,
                              const glm::vec3& pathOffset, const glm::vec3& color, float opacity, bool focused) const {
    if (!_shader || _path.Empty()) {
        return;
    }
    _shader->Use();
    _shader->SetMat4("projection", projection);
    _shader->SetMat4("view", view);
    _shader->SetVec3("cameraPos", cameraPos);
    _shader->SetFloat("ribbonWidth", focused ? 2.8f : 1.6f);
    _shader->SetVec3("pathOffset", pathOffset);
    _shader->SetVec3("lineColor", color);
    _shader->SetFloat("uBaseOpacity", opacity);
    _shader->SetInt("uFocused", focused ? 1 : 0);
    _path.Draw();
}

void MissionPathRenderer::DrawProbe(const glm::mat4& projection, const glm::mat4& view,
                                    const glm::vec3& cameraPos, const glm::vec3& cameraRight,
                                    const glm::vec3& cameraUp, const glm::vec3& probePos,
                                    const glm::vec3& color) const {
    if (!_shader) {
        return;
    }
    const float dist = std::max(glm::length(cameraPos - probePos), 8.0f);
    const float size = kProbeSize * std::clamp(dist / 120.0f, 0.6f, 8.0f);
    const glm::vec3 right = glm::normalize(cameraRight) * size;
    const glm::vec3 up = glm::normalize(cameraUp) * size;
    const glm::vec3 tangent = glm::normalize(cameraPos - probePos);
    const glm::vec3 corners[4] = {probePos - right - up, probePos + right - up, probePos - right + up, probePos + right + up};
    std::vector<RibbonVertex> verts;
    LineStripBatch::AppendQuad(verts, corners, tangent);
    _probe.Stream(verts);

    _shader->Use();
    _shader->SetMat4("projection", projection);
    _shader->SetMat4("view", view);
    _shader->SetVec3("cameraPos", cameraPos);
    _shader->SetFloat("ribbonWidth", 0.15f);
    _shader->SetVec3("pathOffset", glm::vec3(0.0f)); // the quad is streamed in the render frame
    _shader->SetVec3("lineColor", color);
    _shader->SetFloat("uBaseOpacity", 0.95f);
    _shader->SetInt("uFocused", 1);
    _probe.Draw();
}
