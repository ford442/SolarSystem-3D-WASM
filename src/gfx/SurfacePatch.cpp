#include "SurfacePatch.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace SurfacePatch {

namespace {

/** A corner of a piece: barycentric in its facet, and its world offset from the eye. */
struct Corner {
    glm::dvec3 bary;
    glm::dvec3 world;
};

Corner Midpoint(const Corner& a, const Corner& b) {
    return {0.5 * (a.bary + b.bary), 0.5 * (a.world + b.world)};
}

double LongestEdge(const glm::dvec3& a, const glm::dvec3& b, const glm::dvec3& c) {
    return std::max({glm::distance(a, b), glm::distance(b, c), glm::distance(c, a)});
}

bool NeedsSplit(const Corner& a, const Corner& b, const Corner& c, double minDistance) {
    const double distance = DistanceToTriangle(glm::dvec3(0.0), a.world, b.world, c.world);
    return LongestEdge(a.world, b.world, c.world) > kRefineRatio * std::max(distance, minDistance);
}

class Builder {
public:
    Builder(const std::vector<Vertex>& vertices, const glm::dvec3& eyeModel, double minDistance,
            std::vector<Vertex>& out)
        : _vertices(vertices), _eyeModel(eyeModel), _minDistance(minDistance), _out(out) {}

    void Refine(const std::array<unsigned, 3>& facet, const Corner& a, const Corner& b, const Corner& c, int depth) {
        if (depth >= kMaxDepth || !NeedsSplit(a, b, c, _minDistance)) {
            Emit(facet, a);
            Emit(facet, b);
            Emit(facet, c);
            return;
        }
        const Corner ab = Midpoint(a, b), bc = Midpoint(b, c), ca = Midpoint(c, a);
        Refine(facet, a, ab, ca, depth + 1);
        Refine(facet, ab, b, bc, depth + 1);
        Refine(facet, ca, bc, c, depth + 1);
        Refine(facet, ab, bc, ca, depth + 1);
    }

private:
    void Emit(const std::array<unsigned, 3>& facet, const Corner& corner) {
        const Vertex& v0 = _vertices[facet[0]];
        const Vertex& v1 = _vertices[facet[1]];
        const Vertex& v2 = _vertices[facet[2]];
        const glm::dvec3& w = corner.bary;
        const auto blend = [&w](const auto& x, const auto& y, const auto& z) {
            return static_cast<float>(w.x) * x + static_cast<float>(w.y) * y + static_cast<float>(w.z) * z;
        };
        Vertex vertex;
        // Offsets from the eye taken in double before the float conversion.
        vertex.position = glm::vec3(w.x * (glm::dvec3(v0.position) - _eyeModel) +
                                    w.y * (glm::dvec3(v1.position) - _eyeModel) +
                                    w.z * (glm::dvec3(v2.position) - _eyeModel));
        vertex.normal = blend(v0.normal, v1.normal, v2.normal);
        vertex.textureCoords = blend(v0.textureCoords, v1.textureCoords, v2.textureCoords);
        vertex.tangent = blend(v0.tangent, v1.tangent, v2.tangent);
        vertex.bitangent = blend(v0.bitangent, v1.bitangent, v2.bitangent);
        _out.push_back(vertex);
    }

    const std::vector<Vertex>& _vertices;
    glm::dvec3 _eyeModel;
    double _minDistance;
    std::vector<Vertex>& _out;
};

} // namespace

double DistanceToTriangle(const glm::dvec3& p, const glm::dvec3& a, const glm::dvec3& b, const glm::dvec3& c) {
    // Ericson, Real-Time Collision Detection §5.1.5: find the Voronoi region of p.
    const glm::dvec3 ab = b - a, ac = c - a, ap = p - a;
    const double d1 = glm::dot(ab, ap), d2 = glm::dot(ac, ap);
    if (d1 <= 0.0 && d2 <= 0.0) {
        return glm::distance(p, a);
    }
    const glm::dvec3 bp = p - b;
    const double d3 = glm::dot(ab, bp), d4 = glm::dot(ac, bp);
    if (d3 >= 0.0 && d4 <= d3) {
        return glm::distance(p, b);
    }
    const double vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0) {
        return glm::distance(p, a + ab * (d1 / (d1 - d3)));
    }
    const glm::dvec3 cp = p - c;
    const double d5 = glm::dot(ab, cp), d6 = glm::dot(ac, cp);
    if (d6 >= 0.0 && d5 <= d6) {
        return glm::distance(p, c);
    }
    const double vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0) {
        return glm::distance(p, a + ac * (d2 / (d2 - d6)));
    }
    const double va = d3 * d6 - d5 * d4;
    if (va <= 0.0 && d4 - d3 >= 0.0 && d5 - d6 >= 0.0) {
        return glm::distance(p, b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6))));
    }
    const double denominator = va + vb + vc;
    if (denominator == 0.0) {
        return std::min({glm::distance(p, a), glm::distance(p, b), glm::distance(p, c)}); // degenerate
    }
    return glm::distance(p, a + ab * (vb / denominator) + ac * (vc / denominator));
}

bool InReach(const glm::dmat3& shape, const glm::dvec3& eyeModel, double boundingRadius, double maxEdge,
             double minDistance) {
    double minScale = glm::length(shape[0]), maxScale = minScale;
    for (int i = 1; i < 3; ++i) {
        minScale = std::min(minScale, glm::length(shape[i]));
        maxScale = std::max(maxScale, glm::length(shape[i]));
    }
    // Every facet lies inside the sphere through the furthest vertex, so the eye is at least
    // its height above that sphere from all of them.
    const double above = std::max(0.0, glm::length(eyeModel) - boundingRadius) * minScale;
    return maxEdge * maxScale > kRefineRatio * std::max(above, minDistance);
}

std::vector<Vertex> Build(const std::vector<Vertex>& vertices, const std::vector<unsigned>& indices,
                          const glm::dmat3& shape, const glm::dvec3& eyeModel, double minDistance) {
    std::vector<Vertex> patch;
    Builder builder(vertices, eyeModel, minDistance, patch);
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        const std::array<unsigned, 3> facet{indices[i], indices[i + 1], indices[i + 2]};
        if (facet[0] >= vertices.size() || facet[1] >= vertices.size() || facet[2] >= vertices.size()) {
            continue;
        }
        std::array<Corner, 3> corners;
        for (int k = 0; k < 3; ++k) {
            corners[k].bary = glm::dvec3(0.0);
            corners[k].bary[k] = 1.0;
            corners[k].world = shape * (glm::dvec3(vertices[facet[k]].position) - eyeModel);
        }
        if (NeedsSplit(corners[0], corners[1], corners[2], minDistance)) {
            builder.Refine(facet, corners[0], corners[1], corners[2], 0);
        }
    }
    return patch;
}

} // namespace SurfacePatch
