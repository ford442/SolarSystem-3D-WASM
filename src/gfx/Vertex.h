#ifndef SOLARSYSTEM_GFX_VERTEX_H
#define SOLARSYSTEM_GFX_VERTEX_H
#include <glm/glm.hpp>

/** One vertex of a body mesh (locations 0–4 of planetLighting.vs). GL-free, so CPU-side
 *  geometry such as gfx/SurfacePatch can build them without a context. */
struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 textureCoords;
    glm::vec3 tangent;
    glm::vec3 bitangent;
};

#endif // SOLARSYSTEM_GFX_VERTEX_H
