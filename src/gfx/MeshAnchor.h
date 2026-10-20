#ifndef SOLARSYSTEM_MESHANCHOR_H
#define SOLARSYSTEM_MESHANCHOR_H
#include <glm/glm.hpp>

/**
 * Relative-to-eye vertex placement for a mesh drawn close up (the NEAR_SURFACE permutation,
 * docs/ARCHITECTURE.md §11.1).
 *
 * `model * aPos` adds the mesh-sized vertex offset to the body's render-frame translation in
 * float. On realistic scale Earth's centre is 0.08 units from a camera on its surface, where
 * a float is spaced 7.5e-9 units — 0.6 m — so the ground under the camera jitters by that
 * much as it moves. Instead the shader computes
 *
 *     vFragPos = shape * (aPos - anchor) + anchorOffset
 *
 * where `anchor` is the eye in model space (rounded to float) and `anchorOffset` the render-
 * frame position of that rounded point, both derived here in double. For a vertex near the
 * eye `aPos - anchor` is small and exact, so the position comes out as precise as the eye's.
 * A plain subtraction, with no float-float trick an optimising compiler could reassociate.
 */
struct MeshAnchor {
    glm::vec3 anchor{0.0f};       // the eye in model space
    glm::vec3 anchorOffset{0.0f}; // render-frame position of `anchor`
};

/**
 * `shape` is the model matrix's upper 3×3 as the shader sees it (float), `eyeFromBody` the
 * eye's world position minus the body's (double), `eyeRender` the eye in the render frame
 * (zero for the mono camera, which the render origin sits on).
 */
inline MeshAnchor ComputeMeshAnchor(const glm::mat3& shape, const glm::dvec3& eyeFromBody, const glm::dvec3& eyeRender) {
    const glm::dmat3 shapeD(shape);
    const glm::dvec3 exact = glm::inverse(shapeD) * eyeFromBody;
    MeshAnchor result;
    result.anchor = glm::vec3(exact);
    // shape * (aPos - anchor) + offset == shape * (aPos - exact) + eyeRender, which is the
    // vertex's render-frame position: the offset absorbs the anchor's float rounding.
    result.anchorOffset = glm::vec3(eyeRender + shapeD * (glm::dvec3(result.anchor) - exact));
    return result;
}

#endif // SOLARSYSTEM_MESHANCHOR_H
