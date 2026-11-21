#include "OrbitPathRenderer.h"

#include <cmath>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

OrbitPathRenderer::OrbitPathRenderer() {
    std::vector<float> verts;
    verts.reserve(static_cast<size_t>(kSegmentCount) * 3);
    for (int i = 0; i < kSegmentCount; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kSegmentCount) * static_cast<float>(2.0 * M_PI);
        verts.push_back(std::cos(t));
        verts.push_back(0.0f);
        verts.push_back(std::sin(t));
    }

    _vao = GlVertexArray::Create();
    _vbo = GlBuffer::Create();
    glBindVertexArray(_vao.get());
    glBindBuffer(GL_ARRAY_BUFFER, _vbo.get());
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * sizeof(float)), verts.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), reinterpret_cast<void*>(0));
    glBindVertexArray(0);

    _shader = std::make_unique<Shader>("resource/shaders/orbitPath.vs", "resource/shaders/orbitPath.fs");
}

void OrbitPathRenderer::Draw(const glm::mat4& projection, const glm::mat4& view, const glm::mat4& model,
                             const glm::vec4& color) const {
    if (!_shader || !_vao) {
        return;
    }

    _shader->Use();
    _shader->SetMat4("projection", projection);
    _shader->SetMat4("view", view);
    _shader->SetMat4("model", model);
    _shader->SetVec4("color", color);

    glBindVertexArray(_vao.get());
    glDrawArrays(GL_LINE_LOOP, 0, kSegmentCount);
    glBindVertexArray(0);
}
