#pragma once

#include "MagneticFieldParams.h"
#include "MagneticFieldTracer.h"
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
    void AddBody(OrbitLayout::Body body, const MagneticFieldParams& params);
    bool Empty() const { return _batches.empty(); }

    void Draw(const glm::mat4& projection, const glm::mat4& view, const glm::mat4& model,
              OrbitLayout::Body body, const glm::vec3& cameraPosition, float zCoef, float timeSeconds) const;

private:
    struct Vertex {
        glm::vec3 position;
        glm::vec3 tangent;
        float arcLength;
        float side;
    };

    struct Batch {
        OrbitLayout::Body body = OrbitLayout::Body::Sun;
        MagneticFieldParams params;
        GLuint vao = 0;
        GLuint vbo = 0;
        GLsizei vertexCount = 0;
    };

    void DestroyBatch(Batch& batch);
    static std::vector<Vertex> BuildRibbon(const std::vector<MagneticFieldLine>& lines);

    std::vector<Batch> _batches;
    std::unique_ptr<Shader> _shader;
};
