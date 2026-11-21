#ifndef SOLARSYSTEM_GFX_SURFACEPATCH_H
#define SOLARSYSTEM_GFX_SURFACEPATCH_H
#include "Vertex.h"
#include <glm/glm.hpp>
#include <vector>

/**
 * The facets under a camera metres above a coarse body mesh, re-tessellated toward the eye
 * (docs/ARCHITECTURE.md §11.1). GL-free; Renderer::DrawSurfacePatch draws the result.
 *
 * sphere.obj's facets are ~600 km across on realistic Earth. When one passes half a metre
 * under the eye, the GPU clips it by interpolating between corners hundreds of kilometres
 * away in float, which puts the clipped edges a centimetre or so off: a few per cent of the
 * screen at that range, seen as holes along the screen borders. The error grows with facet
 * size over distance, so every facet that comes within size / kRefineRatio of the eye is
 * split until each piece is at most kRefineRatio times its distance (or the near plane,
 * whichever is larger): about 0.04 px of clipping error at 720 lines.
 *
 * The pieces lie in their facet's plane with barycentric attributes, so they shade as the
 * facet does, and Renderer draws them over the whole mesh, which fills any T-junction
 * crack. Positions are offsets from the eye in model space: a float at the mesh's radius is
 * spaced ~0.75 m on realistic Earth, too coarse for points between vertices.
 */
namespace SurfacePatch {

/** A facet is split while it is larger than this many times its distance from the eye. */
constexpr double kRefineRatio = 1000.0;
/** Splits of one facet at most: 600 km halves to under a metre in 20. */
constexpr int kMaxDepth = 20;

/** Distance from `point` to the triangle abc (its face, edges or corners). */
double DistanceToTriangle(const glm::dvec3& point, const glm::dvec3& a, const glm::dvec3& b, const glm::dvec3& c);

/**
 * Whether any facet can be close enough to split, without visiting them: the eye is less
 * than the longest edge / kRefineRatio above the sphere through the furthest vertex.
 * `shape` maps model offsets to world ones (the model matrix's 3×3), `eyeModel` is the eye
 * in model space, `minDistance` (world) the near plane.
 */
bool InReach(const glm::dmat3& shape, const glm::dvec3& eyeModel, double boundingRadius, double maxEdge,
             double minDistance);

/**
 * The patch for an eye at `eyeModel` over the mesh (`vertices` indexed by `indices` as
 * GL_TRIANGLES): a GL_TRIANGLES list whose positions are offsets from `eyeModel`, in the
 * same winding as the facets. Empty when no facet is close enough.
 */
std::vector<Vertex> Build(const std::vector<Vertex>& vertices, const std::vector<unsigned>& indices,
                          const glm::dmat3& shape, const glm::dvec3& eyeModel, double minDistance);

} // namespace SurfacePatch

#endif // SOLARSYSTEM_GFX_SURFACEPATCH_H
