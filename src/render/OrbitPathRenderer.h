#ifndef SOLARSYSTEM_ORBITPATHRENDERER_H
#define SOLARSYSTEM_ORBITPATHRENDERER_H

#include "../gfx/Shader.h"
#include <glm/glm.hpp>
#include <memory>

/** Unit circle in XZ (GL_LINE_LOOP); scaled/rotated per body via the model matrix. */
class OrbitPathRenderer : public IGpuRestorable {
public:
    OrbitPathRenderer();

    OrbitPathRenderer(const OrbitPathRenderer&) = delete;
    /** After a lost context: the unit circle again (the program restores itself). */
    void RestoreGpu() override { BuildCircle(); }
    OrbitPathRenderer& operator=(const OrbitPathRenderer&) = delete;

    void Draw(const glm::mat4& projection, const glm::mat4& view, const glm::mat4& model,
              const glm::vec4& color) const;

private:
    static constexpr int kSegmentCount = 128;
    void BuildCircle();
    GlVertexArray _vao;
    GlBuffer _vbo;
    std::unique_ptr<Shader> _shader;
};

#endif // SOLARSYSTEM_ORBITPATHRENDERER_H
