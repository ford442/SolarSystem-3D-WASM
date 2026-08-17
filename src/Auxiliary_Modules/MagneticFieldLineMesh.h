#pragma once

#include "MagneticFieldTracer.h"
#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <vector>

/** Centerline sample expanded to a camera-facing ribbon vertex (CPU). */
struct MagneticFieldRibbonVertex {
    glm::vec3 position{0.0f};
    glm::vec3 tangent{0.0f, 1.0f, 0.0f};
    float arcLength = 0.0f;
    float lineUV = 0.0f; // -1…1 across ribbon width
};

/**
 * GPU mesh for precomputed magnetic field lines.
 * Geometry is static: the vertex shader extrudes camera-facing quads.
 */
class MagneticFieldLineMesh {
public:
    MagneticFieldLineMesh() = default;
    ~MagneticFieldLineMesh();

    MagneticFieldLineMesh(const MagneticFieldLineMesh&) = delete;
    MagneticFieldLineMesh& operator=(const MagneticFieldLineMesh&) = delete;
    MagneticFieldLineMesh(MagneticFieldLineMesh&& other) noexcept;
    MagneticFieldLineMesh& operator=(MagneticFieldLineMesh&& other) noexcept;

    /** Expand each polyline segment into two triangles (six vertices). */
    static std::vector<MagneticFieldRibbonVertex> Expand(const std::vector<MagneticFieldLine>& lines);

    void Upload(const std::vector<MagneticFieldLine>& lines);
    void UploadVertices(const std::vector<MagneticFieldRibbonVertex>& verts);
    void Destroy();
    void Draw() const;

    bool Empty() const { return _vertexCount <= 0; }
    int VertexCount() const { return _vertexCount; }

private:
    unsigned int _vao = 0;
    unsigned int _vbo = 0;
    int _vertexCount = 0;
};

inline std::vector<MagneticFieldRibbonVertex> MagneticFieldLineMesh::Expand(
    const std::vector<MagneticFieldLine>& lines) {
    std::vector<MagneticFieldRibbonVertex> verts;
    for (const auto& line : lines) {
        if (line.samples.size() < 2) {
            continue;
        }
        for (std::size_t i = 0; i + 1 < line.samples.size(); ++i) {
            const auto& a = line.samples[i];
            const auto& b = line.samples[i + 1];
            glm::vec3 tangent = b.position - a.position;
            const float tlen = glm::length(tangent);
            if (tlen < 1.0e-6f) {
                continue;
            }
            tangent /= tlen;

            const MagneticFieldRibbonVertex corners[4] = {
                {a.position, tangent, a.arcLength, -1.0f},
                {a.position, tangent, a.arcLength, 1.0f},
                {b.position, tangent, b.arcLength, -1.0f},
                {b.position, tangent, b.arcLength, 1.0f},
            };
            // two triangles: 0-1-2, 1-3-2
            verts.push_back(corners[0]);
            verts.push_back(corners[1]);
            verts.push_back(corners[2]);
            verts.push_back(corners[1]);
            verts.push_back(corners[3]);
            verts.push_back(corners[2]);
        }
    }
    return verts;
}
