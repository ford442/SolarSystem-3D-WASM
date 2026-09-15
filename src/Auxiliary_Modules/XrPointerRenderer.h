#ifndef SOLARSYSTEM_XRPOINTERRENDERER_H
#define SOLARSYSTEM_XRPOINTERRENDERER_H

#include "Shader.h"
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
    ~XrPointerRenderer();

    XrPointerRenderer(const XrPointerRenderer&) = delete;
    XrPointerRenderer& operator=(const XrPointerRenderer&) = delete;

    void SetRay(int hand, const glm::vec3& origin, const glm::vec3& direction, bool visible);
    void Clear();
    bool AnyVisible() const { return _rays[0].visible || _rays[1].visible; }

    void Draw(const glm::mat4& projection, const glm::mat4& view, const glm::vec3& cameraPos,
              float zCoef) const;

private:
    struct Vertex {
        glm::vec3 position{0.0f};
        glm::vec3 tangent{0.0f, 0.0f, -1.0f};
        float arcLength = 0.0f;
        float lineUV = 0.0f;
    };

    Ray _rays[2];
    mutable GLuint _vao = 0;
    mutable GLuint _vbo = 0;
    std::unique_ptr<Shader> _shader;
};

#endif // SOLARSYSTEM_XRPOINTERRENDERER_H
