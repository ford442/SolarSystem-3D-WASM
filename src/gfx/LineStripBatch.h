#ifndef SOLARSYSTEM_GFX_LINESTRIPBATCH_H
#define SOLARSYSTEM_GFX_LINESTRIPBATCH_H
#include "GlHandle.h"
#include <glm/glm.hpp>
#include <vector>

/**
 * One vertex of a screen-space ribbon: the layout every `missionPath.vs`-style line shader
 * reads (location 0 position, 1 tangent, 2 arc length, 3 across-ribbon coordinate -1…1).
 */
struct RibbonVertex {
    glm::vec3 position{0.0f};
    glm::vec3 tangent{0.0f, 1.0f, 0.0f};
    float arcLength = 0.0f;
    float lineUV = 0.0f;
};

/**
 * The VAO/VBO behind every ribbon in the scene (mission paths, magnetic field lines, XR
 * pointer rays, the probe marker). The vertex shader extrudes each segment sideways, so a
 * segment is two triangles (six vertices) and the batch draws GL_TRIANGLES.
 */
class LineStripBatch {
public:
    /** Append the six vertices of segment a→b (tangent supplied, arc length at each end). */
    static void AppendSegment(std::vector<RibbonVertex>& out, const glm::vec3& a, const glm::vec3& b,
                              const glm::vec3& tangent, float arcA, float arcB);
    /** Append a camera-facing quad as the same six-vertex shape (the probe marker). */
    static void AppendQuad(std::vector<RibbonVertex>& out, const glm::vec3 corners[4], const glm::vec3& normal);

    /** Replace the contents with a static buffer of `verts` (empty clears). */
    void Upload(const std::vector<RibbonVertex>& verts);
    /**
     * Overwrite the first `verts.size()` vertices of a dynamic buffer, growing it if needed —
     * for the per-frame ribbons (XR rays, probe marker). Draw() then draws exactly these.
     */
    void Stream(const std::vector<RibbonVertex>& verts);
    void Clear();
    void Draw() const;

    bool Empty() const { return _vertexCount <= 0; }
    int VertexCount() const { return _vertexCount; }

private:
    void Allocate(int capacity, const RibbonVertex* data, GLenum usage);

    GlVertexArray _vao;
    GlBuffer _vbo;
    int _vertexCount = 0;
    int _capacity = 0;
};

// Inline so GL-free code (the field-line tests) can expand ribbons without linking the batch.
inline void LineStripBatch::AppendSegment(std::vector<RibbonVertex>& out, const glm::vec3& a, const glm::vec3& b,
                                          const glm::vec3& tangent, float arcA, float arcB) {
    const RibbonVertex corners[4] = {
        {a, tangent, arcA, -1.0f},
        {a, tangent, arcA, 1.0f},
        {b, tangent, arcB, -1.0f},
        {b, tangent, arcB, 1.0f},
    };
    // two triangles: 0-1-2, 1-3-2
    out.push_back(corners[0]);
    out.push_back(corners[1]);
    out.push_back(corners[2]);
    out.push_back(corners[1]);
    out.push_back(corners[3]);
    out.push_back(corners[2]);
}

inline void LineStripBatch::AppendQuad(std::vector<RibbonVertex>& out, const glm::vec3 corners[4], const glm::vec3& normal) {
    const RibbonVertex v[4] = {
        {corners[0], normal, 0.0f, -1.0f},
        {corners[1], normal, 0.0f, 1.0f},
        {corners[2], normal, 1.0f, -1.0f},
        {corners[3], normal, 1.0f, 1.0f},
    };
    out.push_back(v[0]);
    out.push_back(v[1]);
    out.push_back(v[2]);
    out.push_back(v[1]);
    out.push_back(v[3]);
    out.push_back(v[2]);
}

#endif // SOLARSYSTEM_GFX_LINESTRIPBATCH_H
