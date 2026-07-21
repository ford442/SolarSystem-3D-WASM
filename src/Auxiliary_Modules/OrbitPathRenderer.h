#ifndef SOLARSYSTEM_ORBITPATHRENDERER_H
#define SOLARSYSTEM_ORBITPATHRENDERER_H

#include "Shader.h"
#include <glm/glm.hpp>
#include <memory>

/** Unit circle in XZ (GL_LINE_LOOP); scaled/rotated per body via the model matrix. */
class OrbitPathRenderer {
public:
    OrbitPathRenderer();
    ~OrbitPathRenderer();

    OrbitPathRenderer(const OrbitPathRenderer&) = delete;
    OrbitPathRenderer& operator=(const OrbitPathRenderer&) = delete;

    void Draw(const glm::mat4& projection, const glm::mat4& view, const glm::mat4& model,
              const glm::vec4& color) const;

private:
    static constexpr int kSegmentCount = 128;
    GLuint _vao = 0;
    GLuint _vbo = 0;
    std::unique_ptr<Shader> _shader;
};

#endif // SOLARSYSTEM_ORBITPATHRENDERER_H
