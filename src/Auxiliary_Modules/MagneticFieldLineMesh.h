#pragma once

#include "MagneticFieldTracer.h"
#include "../gfx/LineStripBatch.h"
#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <vector>

/** Centerline sample expanded to a camera-facing ribbon vertex (CPU). */
using MagneticFieldRibbonVertex = RibbonVertex;

/**
 * GPU mesh for precomputed magnetic field lines.
 * Geometry is static: the vertex shader extrudes camera-facing quads.
 */
class MagneticFieldLineMesh {
public:
    /** Expand each polyline segment into two triangles (six vertices). */
    static std::vector<MagneticFieldRibbonVertex> Expand(const std::vector<MagneticFieldLine>& lines);

    void Upload(const std::vector<MagneticFieldLine>& lines) { _batch.Upload(Expand(lines)); }
    void UploadVertices(const std::vector<MagneticFieldRibbonVertex>& verts) { _batch.Upload(verts); }
    void Destroy() { _batch.Clear(); }
    void Draw() const { _batch.Draw(); }

    bool Empty() const { return _batch.Empty(); }
    int VertexCount() const { return _batch.VertexCount(); }

private:
    LineStripBatch _batch;
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

            LineStripBatch::AppendSegment(verts, a.position, b.position, tangent, a.arcLength, b.arcLength);
        }
    }
    return verts;
}
