#ifndef SOLARSYSTEM_MISSIONPATHRENDERER_H
#define SOLARSYSTEM_MISSIONPATHRENDERER_H

#include "../gfx/Shader.h"
#include "../gfx/LineStripBatch.h"
#include <glm/glm.hpp>
#include <memory>
#include <vector>

/**
 * Width overrides. The defaults suit a probe path seen from far off (at least ~2 scene units
 * wide); a chase camera close to its craft wants a ribbon and marker of constant on-screen
 * size instead: `minDistScale` ~0 and widths in units per 80 units of distance.
 */
struct MissionPathStyle {
    float ribbonWidth = -1.0f; // < 0: 2.8 focused, 1.6 otherwise
    float minDistScale = 1.0f;
    float probeAngularSize = -1.0f; // marker half-size over its distance; < 0: the probe default
};

/** Camera-facing ribbon polyline for one baked mission path (GL_STATIC_DRAW). */
class MissionPathRenderer {
public:
    MissionPathRenderer();

    MissionPathRenderer(const MissionPathRenderer&) = delete;
    MissionPathRenderer& operator=(const MissionPathRenderer&) = delete;

    void Clear();
    /** Positions relative to an upload origin; Draw's `pathOffset` is that origin in the render frame. */
    void Upload(const std::vector<glm::vec3>& scenePositions);
    bool Empty() const { return _path.Empty(); }

    using Style = MissionPathStyle;

    void Draw(const glm::mat4& projection, const glm::mat4& view, const glm::vec3& cameraPos,
              const glm::vec3& pathOffset, const glm::vec3& color, float opacity, bool focused,
              const Style& style = {}) const;

    /** `probePos` in the render frame. */
    void DrawProbe(const glm::mat4& projection, const glm::mat4& view, const glm::vec3& cameraPos,
                   const glm::vec3& cameraRight, const glm::vec3& cameraUp, const glm::vec3& probePos,
                   const glm::vec3& color, const Style& style = {}) const;

private:
    static std::vector<RibbonVertex> Expand(const std::vector<glm::vec3>& positions);

    LineStripBatch _path;
    mutable LineStripBatch _probe;
    std::unique_ptr<Shader> _shader;
};

#endif // SOLARSYSTEM_MISSIONPATHRENDERER_H
