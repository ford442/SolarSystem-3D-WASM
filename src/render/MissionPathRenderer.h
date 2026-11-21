#ifndef SOLARSYSTEM_MISSIONPATHRENDERER_H
#define SOLARSYSTEM_MISSIONPATHRENDERER_H

#include "../gfx/Shader.h"
#include "../gfx/LineStripBatch.h"
#include <glm/glm.hpp>
#include <memory>
#include <vector>

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

    void Draw(const glm::mat4& projection, const glm::mat4& view, const glm::vec3& cameraPos,
              const glm::vec3& pathOffset, const glm::vec3& color, float opacity, bool focused) const;

    /** `probePos` in the render frame. */
    void DrawProbe(const glm::mat4& projection, const glm::mat4& view, const glm::vec3& cameraPos,
                   const glm::vec3& cameraRight, const glm::vec3& cameraUp,
                   const glm::vec3& probePos, const glm::vec3& color) const;

private:
    static std::vector<RibbonVertex> Expand(const std::vector<glm::vec3>& positions);

    LineStripBatch _path;
    mutable LineStripBatch _probe;
    std::unique_ptr<Shader> _shader;
};

#endif // SOLARSYSTEM_MISSIONPATHRENDERER_H
