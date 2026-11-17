#pragma once

#include "MagneticFieldLineMesh.h"
#include "../astro/MagneticFieldParams.h"
#include "../gfx/Shader.h"
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
              float timeSeconds, float ribbonWidthScale = 1.0f, float opacityScale = 1.0f) const;

    /**
     * How the solar wind deforms the next Draw's lines: the unit vector to the Sun and the
     * standoff over its quiet value (magneticFieldLine.vs). Ratio 1 draws the plain dipole.
     */
    void SetCompression(const glm::vec3& sunward, float standoffRatio) {
        _compression = glm::vec4(sunward, standoffRatio);
    }

private:
    struct Batch {
        OrbitLayout::Body body = OrbitLayout::Body::Sun;
        MagneticFieldLineMesh mesh;
    };

    std::vector<Batch> _batches;
    std::unique_ptr<Shader> _shader;
    glm::vec4 _compression{1.0f, 0.0f, 0.0f, 1.0f};
};
