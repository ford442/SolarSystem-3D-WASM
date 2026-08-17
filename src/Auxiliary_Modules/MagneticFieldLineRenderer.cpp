#include "MagneticFieldLineRenderer.h"

MagneticFieldLineRenderer::MagneticFieldLineRenderer() {
    _shader = std::make_unique<Shader>("resource/shaders/magneticFieldLine.vs",
                                       "resource/shaders/magneticFieldLine.fs");
}

MagneticFieldLineRenderer::~MagneticFieldLineRenderer() {
    Clear();
}

void MagneticFieldLineRenderer::Clear() {
    _batches.clear();
}

void MagneticFieldLineRenderer::AddBody(OrbitLayout::Body body, MagneticFieldLineMesh mesh,
                                        const MagneticFieldParams& params) {
    if (!params.enabled || mesh.Empty()) {
        return;
    }
    Batch batch;
    batch.body = body;
    batch.mesh = std::move(mesh);
    _batches.push_back(std::move(batch));
}

void MagneticFieldLineRenderer::Draw(const glm::mat4& projection, const glm::mat4& view, const glm::mat4& model,
                                     const MagneticFieldParams& params, OrbitLayout::Body body,
                                     const glm::vec3& cameraPosition, float zCoef, float timeSeconds) const {
    if (!_shader || !params.enabled) {
        return;
    }

    _shader->Use();
    _shader->SetMat4("projection", projection);
    _shader->SetMat4("view", view);
    _shader->SetMat4("model", model);
    _shader->SetVec3("cameraPos", cameraPosition);
    _shader->SetFloat("zCoef", zCoef);
    _shader->SetFloat("uTime", timeSeconds);
    _shader->SetFloat("ribbonWidth", params.ribbonWidth);
    _shader->SetFloat("uFlowSpeed", params.flowSpeed);
    _shader->SetFloat("uBaseOpacity", params.opacity);
    _shader->SetVec3("lineColor", params.color);

    for (const auto& batch : _batches) {
        if (batch.body != body || batch.mesh.Empty()) {
            continue;
        }
        batch.mesh.Draw();
    }
}
