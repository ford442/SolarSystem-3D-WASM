#ifndef SOLARSYSTEM_MESHKEEPER_H
#define SOLARSYSTEM_MESHKEEPER_H
#include "../assets/ModelCache.h"
#include "Mesh.h"
#include "Shader.h"
#include <memory>
#include <string>
#include <vector>

class MeshHolder {
public:
    /**
     * The model at `path`: parsed by assets/ModelCache (in a job, usually started when its
     * download landed), uploaded here on the main thread.
     */
    explicit MeshHolder(const std::string& path);
    void Draw(const Shader& shader) const; // Отрисовка модели (мешей)
    /** Largest vertex distance from the model origin, in model units (culling bounds). */
    float BoundingRadius() const { return _boundingRadius; }
    /**
     * Every mesh's vertices and triangle indices, kept on the CPU for the camera floor's ray
     * casts (positions, GL-free for core/) and the near-surface patch (whole vertices).
     */
    using Triangles = MeshTriangles;
    /** Shared by copies and by every holder of the same path (the parse is cached). */
    const Triangles& GetTriangles() const { return *_triangles; }

private:
    std::shared_ptr<const Triangles> _triangles = std::make_shared<Triangles>();
    std::vector<Mesh> _meshes;
    float _boundingRadius = 0.0f;
};

#endif //SOLARSYSTEM_MESHKEEPER_H
