#ifndef SOLARSYSTEM_XRPOINTERRENDERER_H
#define SOLARSYSTEM_XRPOINTERRENDERER_H

#include "Shader.h"
#include "../gfx/LineStripBatch.h"
#include <glm/glm.hpp>
#include <memory>

/** Two controller rays as camera-facing ribbons. Hidden when WebXR is inactive. */
class XrPointerRenderer {
public:
    struct Ray {
        glm::vec3 origin{0.0f};
        glm::vec3 direction{0.0f, 0.0f, -1.0f};
        bool visible = false;
    };

    XrPointerRenderer();

    XrPointerRenderer(const XrPointerRenderer&) = delete;
    XrPointerRenderer& operator=(const XrPointerRenderer&) = delete;

    void SetRay(int hand, const glm::vec3& origin, const glm::vec3& direction, bool visible);
    /** Rays are in the render frame; keep them in place when its origin moves by `shift`. */
    void ShiftOrigin(const glm::vec3& shift) {
        _rays[0].origin -= shift;
        _rays[1].origin -= shift;
    }
    void Clear();
    bool AnyVisible() const { return _rays[0].visible || _rays[1].visible; }

    void Draw(const glm::mat4& projection, const glm::mat4& view, const glm::vec3& cameraPos,
              float zCoef) const;

private:
    Ray _rays[2];
    mutable LineStripBatch _ray;
    std::unique_ptr<Shader> _shader;
};

#endif // SOLARSYSTEM_XRPOINTERRENDERER_H
