#include "MissionPathRenderer.h"

#include <cmath>
#include <cstddef>

namespace {

constexpr float kProbeSize = 2.4f;

} // namespace

MissionPathRenderer::MissionPathRenderer() {
    _shader = std::make_unique<Shader>("resource/shaders/missionPath.vs", "resource/shaders/missionPath.fs");
}

MissionPathRenderer::~MissionPathRenderer() {
    Destroy();
    if (_probeVbo) {
        glDeleteBuffers(1, &_probeVbo);
        _probeVbo = 0;
    }
    if (_probeVao) {
        glDeleteVertexArrays(1, &_probeVao);
        _probeVao = 0;
    }
}

void MissionPathRenderer::Destroy() {
    if (_vbo) {
        glDeleteBuffers(1, &_vbo);
        _vbo = 0;
    }
    if (_vao) {
        glDeleteVertexArrays(1, &_vao);
        _vao = 0;
    }
    _vertexCount = 0;
}

void MissionPathRenderer::Clear() {
    Destroy();
}

std::vector<MissionPathRenderer::Vertex> MissionPathRenderer::Expand(
    const std::vector<glm::vec3>& positions) {
    std::vector<Vertex> verts;
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
        const Vertex corners[4] = {
            {a, tangent, arc, -1.0f},
            {a, tangent, arc, 1.0f},
            {b, tangent, nextArc, -1.0f},
            {b, tangent, nextArc, 1.0f},
        };
        verts.push_back(corners[0]);
        verts.push_back(corners[1]);
        verts.push_back(corners[2]);
        verts.push_back(corners[1]);
        verts.push_back(corners[3]);
        verts.push_back(corners[2]);
        arc = nextArc;
    }
    return verts;
}

void MissionPathRenderer::Upload(const std::vector<glm::vec3>& scenePositions) {
    Destroy();
    const std::vector<Vertex> verts = Expand(scenePositions);
    if (verts.empty()) {
        return;
    }
    _vertexCount = static_cast<int>(verts.size());
    glGenVertexArrays(1, &_vao);
    glGenBuffers(1, &_vbo);
    glBindVertexArray(_vao);
    glBindBuffer(GL_ARRAY_BUFFER, _vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * sizeof(Vertex)), verts.data(),
                 GL_STATIC_DRAW);
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

void MissionPathRenderer::Draw(const glm::mat4& projection, const glm::mat4& view, const glm::vec3& cameraPos,
                              float zCoef, const glm::vec3& color, float opacity, bool focused) const {
    if (!_shader || _vao == 0 || _vertexCount <= 0) {
        return;
    }
    _shader->Use();
    _shader->SetMat4("projection", projection);
    _shader->SetMat4("view", view);
    _shader->SetVec3("cameraPos", cameraPos);
    _shader->SetFloat("ribbonWidth", focused ? 2.8f : 1.6f);
    _shader->SetFloat("zCoef", zCoef);
    _shader->SetVec3("lineColor", color);
    _shader->SetFloat("uBaseOpacity", opacity);
    _shader->SetInt("uFocused", focused ? 1 : 0);
    glBindVertexArray(_vao);
    glDrawArrays(GL_TRIANGLES, 0, _vertexCount);
    glBindVertexArray(0);
}

void MissionPathRenderer::DrawProbe(const glm::mat4& projection, const glm::mat4& view,
                                    const glm::vec3& cameraPos, const glm::vec3& cameraRight,
                                    const glm::vec3& cameraUp, float zCoef, const glm::vec3& probePos,
                                    const glm::vec3& color) const {
    if (!_shader) {
        return;
    }
    if (_probeVao == 0) {
        glGenVertexArrays(1, &_probeVao);
        glGenBuffers(1, &_probeVbo);
        glBindVertexArray(_probeVao);
        glBindBuffer(GL_ARRAY_BUFFER, _probeVbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(6 * sizeof(Vertex)), nullptr, GL_DYNAMIC_DRAW);
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

    const float dist = std::max(glm::length(cameraPos - probePos), 8.0f);
    const float size = kProbeSize * std::clamp(dist / 120.0f, 0.6f, 8.0f);
    const glm::vec3 right = glm::normalize(cameraRight) * size;
    const glm::vec3 up = glm::normalize(cameraUp) * size;
    const glm::vec3 tangent = glm::normalize(cameraPos - probePos);
    const Vertex corners[4] = {
        {probePos - right - up, tangent, 0.0f, -1.0f},
        {probePos + right - up, tangent, 0.0f, 1.0f},
        {probePos - right + up, tangent, 1.0f, -1.0f},
        {probePos + right + up, tangent, 1.0f, 1.0f},
    };
    const Vertex verts[6] = {corners[0], corners[1], corners[2], corners[1], corners[3], corners[2]};
    glBindBuffer(GL_ARRAY_BUFFER, _probeVbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);

    _shader->Use();
    _shader->SetMat4("projection", projection);
    _shader->SetMat4("view", view);
    _shader->SetVec3("cameraPos", cameraPos);
    _shader->SetFloat("ribbonWidth", 0.15f);
    _shader->SetFloat("zCoef", zCoef);
    _shader->SetVec3("lineColor", color);
    _shader->SetFloat("uBaseOpacity", 0.95f);
    _shader->SetInt("uFocused", 1);
    glBindVertexArray(_probeVao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}
