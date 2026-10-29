#ifndef SOLARSYSTEM_GFX_SURFACEPATCHMESH_H
#define SOLARSYSTEM_GFX_SURFACEPATCHMESH_H
#include "GlHandle.h"
#include "Vertex.h"
#include <vector>

/**
 * The GPU side of gfx/SurfacePatch: a dynamic GL_TRIANGLES buffer in Mesh's vertex layout,
 * rewritten whenever the patch under the camera changes.
 */
class SurfacePatchMesh {
public:
    /** Overwrite the buffer with `vertices`, growing it if needed. */
    void Stream(const std::vector<Vertex>& vertices);
    void Draw() const;

private:
    GlVertexArray _vao;
    GlBuffer _vbo;
    int _vertexCount = 0;
    int _capacity = 0;
};

#endif // SOLARSYSTEM_GFX_SURFACEPATCHMESH_H
