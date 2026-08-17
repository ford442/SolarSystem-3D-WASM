#include "MagneticFieldLineRenderer.h"

#include <glm/geometric.hpp>

MagneticFieldLineRenderer::MagneticFieldLineRenderer() {
    _shader = std::make_unique<Shader>("resource/shaders/magneticField.vs",
                                       "resource/shaders/magneticField.fs");
}

MagneticFieldLineRenderer::~MagneticFieldLineRenderer() {
    Clear();
}

void MagneticFieldLineRenderer::DestroyBatch(Batch& batch) {
    if (batch.vbo) {
        glDeleteBuffers(1, &batch.vbo);
        batch.vbo = 0;
    }
    if (batch.vao) {
        glDeleteVertexArrays(1, &batch.vao);
        batch.vao = 0;
    }
    batch.vertexCount = 0;
}

void MagneticFieldLineRenderer::Clear() {
    for (auto& batch : _batches) {
        DestroyBatch(batch);
    }
    _batches.clear();
}

std::vector<MagneticFieldLineRenderer::Vertex> MagneticFieldLineRenderer::BuildRibbon(
    const std::vector<MagneticFieldLine>& lines) {
    std::vector<Vertex> verts;
    for (const auto& line : lines) {
        if (line.samples.size() < 2) {
            continue;
        }
        for (std::size_t i = 0; i + 1 < line.samples.size(); ++i) {
            const auto& a = line.samples[i];
            const auto& b = line.samples[i + 1];
            glm::vec3 tangent = b.position - a.position;
            const float tlen = glm::length(tangent);
            if (tlen < 1.0e-6f) {
                continue;
            }
            tangent /= tlen;

            const Vertex corners[4] = {
                {a.position, tangent, a.arcLength, -1.0f},
                {a.position, tangent, a.arcLength, 1.0f},
                {b.position, tangent, b.arcLength, -1.0f},
                {b.position, tangent, b.arcLength, 1.0f},
            };
            // two triangles: 0-1-2, 1-3-2
            verts.push_back(corners[0]);
            verts.push_back(corners[1]);
            verts.push_back(corners[2]);
            verts.push_back(corners[1]);
            verts.push_back(corners[3]);
            verts.push_back(corners[2]);
        }
    }
    return verts;
}

void MagneticFieldLineRenderer::AddBody(OrbitLayout::Body body, const MagneticFieldParams& params) {
    if (!params.enabled) {
        return;
    }
    const auto lines = MagneticFieldTracer::Trace(params);
    auto verts = BuildRibbon(lines);
    if (verts.empty()) {
        return;
    }

    Batch batch;
    batch.body = body;
    batch.params = params;
    batch.vertexCount = static_cast<GLsizei>(verts.size());

    glGenVertexArrays(1, &batch.vao);
    glGenBuffers(1, &batch.vbo);
    glBindVertexArray(batch.vao);
    glBindBuffer(GL_ARRAY_BUFFER, batch.vbo);
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
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(Vertex, side)));

    glBindVertexArray(0);
    _batches.push_back(batch);
}

void MagneticFieldLineRenderer::Draw(const glm::mat4& projection, const glm::mat4& view, const glm::mat4& model,
                                     OrbitLayout::Body body, const glm::vec3& cameraPosition, float zCoef,
                                     float timeSeconds) const {
    if (!_shader) {
        return;
    }

    _shader->Use();
    _shader->SetMat4("projection", projection);
    _shader->SetMat4("view", view);
    _shader->SetMat4("model", model);
    _shader->SetVec3("cameraPos", cameraPosition);
    _shader->SetFloat("zCoef", zCoef);
    _shader->SetFloat("uTime", timeSeconds);

    for (const auto& batch : _batches) {
        if (batch.body != body || batch.vao == 0 || batch.vertexCount <= 0) {
            continue;
        }
        _shader->SetFloat("ribbonWidth", batch.params.ribbonWidth);
        _shader->SetFloat("flowSpeed", batch.params.flowSpeed);
        _shader->SetFloat("uOpacity", 0.85f);
        _shader->SetVec3("lineColor", batch.params.color);

        glBindVertexArray(batch.vao);
        glDrawArrays(GL_TRIANGLES, 0, batch.vertexCount);
        glBindVertexArray(0);
    }
}
