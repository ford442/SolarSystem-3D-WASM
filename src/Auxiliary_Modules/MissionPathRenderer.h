#ifndef SOLARSYSTEM_MISSIONPATHRENDERER_H
#define SOLARSYSTEM_MISSIONPATHRENDERER_H

#include "Shader.h"
#include <glm/glm.hpp>
#include <memory>
#include <vector>

/** Camera-facing ribbon polyline for one baked mission path (GL_STATIC_DRAW). */
class MissionPathRenderer {
public:
    MissionPathRenderer();
    ~MissionPathRenderer();

    MissionPathRenderer(const MissionPathRenderer&) = delete;
    MissionPathRenderer& operator=(const MissionPathRenderer&) = delete;

    void Clear();
    void Upload(const std::vector<glm::vec3>& scenePositions);
    bool Empty() const { return _vertexCount <= 0; }

    void Draw(const glm::mat4& projection, const glm::mat4& view, const glm::vec3& cameraPos,
              float zCoef, const glm::vec3& color, float opacity, bool focused) const;

    void DrawProbe(const glm::mat4& projection, const glm::mat4& view, const glm::vec3& cameraPos,
                   const glm::vec3& cameraRight, const glm::vec3& cameraUp, float zCoef,
                   const glm::vec3& probePos, const glm::vec3& color) const;

private:
    struct Vertex {
        glm::vec3 position{0.0f};
        glm::vec3 tangent{0.0f, 1.0f, 0.0f};
        float arcLength = 0.0f;
        float lineUV = 0.0f;
    };

    void Destroy();
    static std::vector<Vertex> Expand(const std::vector<glm::vec3>& positions);

    GLuint _vao = 0;
    GLuint _vbo = 0;
    int _vertexCount = 0;
    mutable GLuint _probeVao = 0;
    mutable GLuint _probeVbo = 0;
    std::unique_ptr<Shader> _shader;
};

#endif // SOLARSYSTEM_MISSIONPATHRENDERER_H
