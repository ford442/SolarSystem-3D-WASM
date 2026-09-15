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

XrPointerRenderer::~XrPointerRenderer() {
    if (_vbo) {
        glDeleteBuffers(1, &_vbo);
        _vbo = 0;
    }
    if (_vao) {
        glDeleteVertexArrays(1, &_vao);
        _vao = 0;
    }
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
    if (_vao == 0) {
        glGenVertexArrays(1, &_vao);
        glGenBuffers(1, &_vbo);
        glBindVertexArray(_vao);
        glBindBuffer(GL_ARRAY_BUFFER, _vbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(12 * sizeof(Vertex)), nullptr, GL_DYNAMIC_DRAW);
        const GLsizei stride = static_cast<GLsizei>(sizeof(Vertex));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(Vertex, position)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(Vertex, tangent)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(Vertex, arcLength)));
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(Vertex, lineUV)));
        glBindVertexArray(0);
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
        const Vertex corners[4] = {
            {a, tangent, 0.0f, -1.0f},
            {a, tangent, 0.0f, 1.0f},
            {b, tangent, kRayLength, -1.0f},
            {b, tangent, kRayLength, 1.0f},
        };
        const Vertex verts[6] = {corners[0], corners[1], corners[2], corners[1], corners[3], corners[2]};
        glBindBuffer(GL_ARRAY_BUFFER, _vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
        _shader->SetVec3("lineColor", kHandColor[hand]);
        glBindVertexArray(_vao);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
    }
}
