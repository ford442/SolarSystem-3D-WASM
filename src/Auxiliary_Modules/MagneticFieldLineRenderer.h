#pragma once

#include "MagneticFieldLineMesh.h"
#include "MagneticFieldParams.h"
#include "Shader.h"
#include <glm/glm.hpp>
#include <memory>
#include <vector>

class MagneticFieldLineRenderer {
public:
    MagneticFieldLineRenderer();
    ~MagneticFieldLineRenderer();

    MagneticFieldLineRenderer(const MagneticFieldLineRenderer&) = delete;
    MagneticFieldLineRenderer& operator=(const MagneticFieldLineRenderer&) = delete;

    void Clear();
    void AddBody(OrbitLayout::Body body, MagneticFieldLineMesh mesh, const MagneticFieldParams& params);
    bool Empty() const { return _batches.empty(); }

    /**
     * Draw prebuilt ribbons for `body`.
     * `params` supplies color, flowSpeed, opacity, ribbonWidth, and enabled.
     * Only `timeSeconds` is expected to change every frame (GPU flow animation).
     */
    void Draw(const glm::mat4& projection, const glm::mat4& view, const glm::mat4& model,
              const MagneticFieldParams& params, OrbitLayout::Body body, const glm::vec3& cameraPosition,
              float zCoef, float timeSeconds) const;

private:
    struct Batch {
        OrbitLayout::Body body = OrbitLayout::Body::Sun;
        MagneticFieldParams params;
        MagneticFieldLineMesh mesh;
    };

    std::vector<Batch> _batches;
    std::unique_ptr<Shader> _shader;
};
