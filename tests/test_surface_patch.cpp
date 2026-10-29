#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Solar_System/SceneScale.h"
#include "gfx/SurfacePatch.h"

namespace {

constexpr double kMetre = 1.0e-3 / SceneScale::kKmPerSceneUnit;

/** Realistic Earth's model matrix 3×3: sphere.obj (radius 2) at 6371 km, tilted and spun. */
glm::dmat3 EarthShape() {
    const double scale = SceneScale::KmToScene(6371.0) / 2.0;
    glm::dmat4 m = glm::rotate(glm::dmat4(1.0), glm::radians(23.4), glm::dvec3(0.0, 0.0, 1.0));
    m = glm::rotate(m, glm::radians(131.7), glm::dvec3(0.0, 1.0, 0.0));
    return glm::dmat3(m) * scale;
}

/**
 * One facet of a 64×32 sphere of radius 2 (about 600 km across on realistic Earth), with
 * texture coordinates (0,0), (1,0), (0,1) so a vertex's uv is its barycentric position.
 */
std::vector<Vertex> OneFacet() {
    const double step = M_PI / 32.0;
    const auto at = [](double phi, double theta) {
        return glm::vec3(2.0 * std::sin(phi) * std::cos(theta), 2.0 * std::cos(phi), 2.0 * std::sin(phi) * std::sin(theta));
    };
    std::vector<Vertex> facet(3);
    facet[0].position = at(10 * step, 7 * step);
    facet[1].position = at(11 * step, 7 * step);
    facet[2].position = at(10 * step, 8 * step);
    facet[0].textureCoords = {0.0f, 0.0f};
    facet[1].textureCoords = {1.0f, 0.0f};
    facet[2].textureCoords = {0.0f, 1.0f};
    for (Vertex& v : facet) {
        v.normal = glm::normalize(v.position);
        v.tangent = {1.0f, 0.0f, 0.0f};
        v.bitangent = {0.0f, 0.0f, 1.0f};
    }
    return facet;
}

struct Eye {
    glm::dvec3 model;   // the eye in model space
    glm::dvec3 footprint; // the facet point under it, model space
};

/** `metres` above the facet point at barycentric (0.31, 0.27, 0.42). */
Eye EyeAbove(const std::vector<Vertex>& facet, const glm::dmat3& shape, double metres) {
    const glm::dvec3 a(facet[0].position), b(facet[1].position), c(facet[2].position);
    Eye eye;
    eye.footprint = 0.31 * a + 0.27 * b + 0.42 * c;
    const glm::dvec3 worldNormal = glm::normalize(glm::transpose(glm::inverse(shape)) * glm::cross(b - a, c - a));
    eye.model = eye.footprint + glm::inverse(shape) * (worldNormal * (metres * kMetre));
    return eye;
}

double Area(const glm::dvec3& a, const glm::dvec3& b, const glm::dvec3& c) {
    return 0.5 * glm::length(glm::cross(b - a, c - a));
}

const std::vector<unsigned> kOneFacetIndices{0, 1, 2};

} // namespace

TEST(SurfacePatch, DistanceToTriangleCoversFaceEdgesAndCorners) {
    const glm::dvec3 a(0.0, 0.0, 0.0), b(4.0, 0.0, 0.0), c(0.0, 4.0, 0.0);
    EXPECT_DOUBLE_EQ(SurfacePatch::DistanceToTriangle({1.0, 1.0, 3.0}, a, b, c), 3.0);  // over the face
    EXPECT_DOUBLE_EQ(SurfacePatch::DistanceToTriangle({2.0, -2.0, 0.0}, a, b, c), 2.0); // beside edge ab
    EXPECT_DOUBLE_EQ(SurfacePatch::DistanceToTriangle({-3.0, -4.0, 0.0}, a, b, c), 5.0); // past corner a
    EXPECT_NEAR(SurfacePatch::DistanceToTriangle({3.0, 3.0, 0.0}, a, b, c), std::sqrt(2.0), 1e-12); // past bc
}

TEST(SurfacePatch, NothingToDoAKilometreUp) {
    const std::vector<Vertex> facet = OneFacet();
    const glm::dmat3 shape = EarthShape();
    const Eye eye = EyeAbove(facet, shape, 1000.0);
    EXPECT_TRUE(SurfacePatch::Build(facet, kOneFacetIndices, shape, eye.model, 500.0 * kMetre).empty());

    // The cheap gate: well above the vertices' sphere it says no without visiting a facet.
    const double scale = glm::length(shape[0]);
    const glm::dvec3 high = glm::normalize(eye.model) * (2.0 + 2000.0 * kMetre / scale);
    EXPECT_FALSE(SurfacePatch::InReach(shape, high, 2.0, 0.2, 500.0 * kMetre));
    EXPECT_TRUE(SurfacePatch::InReach(shape, eye.model, 2.0, 0.2, 0.25 * kMetre));
}

TEST(SurfacePatch, HalfAMetreUpTheFacetIsTiledInPlaceToTheMillimetre) {
    const std::vector<Vertex> facet = OneFacet();
    const glm::dmat3 shape = EarthShape();
    const Eye eye = EyeAbove(facet, shape, 0.5);
    const double nearPlane = 0.25 * kMetre;
    const std::vector<Vertex> patch = SurfacePatch::Build(facet, kOneFacetIndices, shape, eye.model, nearPlane);
    ASSERT_FALSE(patch.empty());
    ASSERT_EQ(patch.size() % 3, 0u);
    EXPECT_LT(patch.size(), 3000u);

    const glm::dvec3 a(facet[0].position), b(facet[1].position), c(facet[2].position);
    const glm::dvec3 facetNormal = glm::cross(shape * (b - a), shape * (c - a));
    double area = 0.0;
    double finest = 1e30;
    for (size_t i = 0; i < patch.size(); i += 3) {
        glm::dvec3 world[3];
        for (int k = 0; k < 3; ++k) {
            const Vertex& v = patch[i + k];
            // Where the vertex should be: the facet point its uv names, offset from the eye.
            const double u = v.textureCoords.x, w = v.textureCoords.y;
            const glm::dvec3 exact = shape * ((1.0 - u - w) * a + u * b + w * c - eye.model);
            world[k] = shape * glm::dvec3(v.position);
            const double tolerance = std::max(1.0e-3 * kMetre, 1.0e-6 * glm::length(exact));
            ASSERT_LT(glm::distance(world[k], exact), tolerance) << "vertex " << i + k;
            EXPECT_GE(u, -1e-6);
            EXPECT_GE(w, -1e-6);
            EXPECT_LE(u + w, 1.0 + 1e-6);
        }
        EXPECT_GT(glm::dot(glm::cross(world[1] - world[0], world[2] - world[0]), facetNormal), 0.0) << "winding";
        area += Area(world[0], world[1], world[2]);

        // Every piece is small enough for its distance, so its clipping error is ~0.04 px.
        const double distance = SurfacePatch::DistanceToTriangle(glm::dvec3(0.0), world[0], world[1], world[2]);
        const double edge = std::max({glm::distance(world[0], world[1]), glm::distance(world[1], world[2]),
                                      glm::distance(world[2], world[0])});
        EXPECT_LE(edge, SurfacePatch::kRefineRatio * std::max(distance, nearPlane) * (1.0 + 1e-9));
        if (distance < 2.0 * kMetre) {
            finest = std::min(finest, edge);
        }
    }
    // The pieces tile the facet, no gaps or overlaps (to the float rounding of the corners
    // 600 km away).
    EXPECT_NEAR(area, Area(shape * a, shape * b, shape * c), 1e-7 * area);
    // Under the eye they are a few hundred metres (kRefineRatio × 0.5 m at most), not 600 km.
    EXPECT_LT(finest, 500.0 * kMetre);
}
