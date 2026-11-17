#ifndef SOLARSYSTEM_ASSETS_MODELCACHE_H
#define SOLARSYSTEM_ASSETS_MODELCACHE_H
#include "../gfx/Vertex.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

/**
 * Every mesh's vertices and triangle indices of one model, kept on the CPU for the camera
 * floor's ray casts (positions) and the near-surface patch (whole vertices).
 */
struct MeshTriangles {
    std::vector<glm::vec3> positions;
    std::vector<Vertex> vertices;
    std::vector<unsigned> indices;
    float maxEdge = 0.0f; // longest triangle edge, model units
};

/** A parsed model, GL-free: what MeshHolder uploads, built in a job (core/JobSystem.h). */
struct MeshPayload {
    struct Material {
        std::string type; // "texture_diffuse", ...
        std::string path;
    };
    struct Part {
        std::vector<Vertex> vertices;
        std::vector<unsigned> indices;
        std::vector<Material> textures;
    };
    std::vector<Part> parts;
    float boundingRadius = 0.0f; // largest vertex distance from the origin, model units
    std::shared_ptr<const MeshTriangles> triangles;
};

/** Assimp-parse a model file's bytes (format from the path's extension). Throws std::runtime_error. */
MeshPayload ParseModel(const std::string& path, const std::vector<std::uint8_t>& bytes);

/**
 * Parsed models by path. Prefetch starts the parse in a job as soon as the file is resident
 * (Application starts one per model as its core download lands, so parsing overlaps the
 * rest of the download); Get returns the payload, waiting for that job or parsing inline if
 * nothing was prefetched. A parsed model stays cached: every body's MeshHolder of
 * sphere.obj shares the one parse. Main thread only.
 */
namespace ModelCache {
void Prefetch(const std::string& path);
std::shared_ptr<const MeshPayload> Get(const std::string& path);
void Clear();
} // namespace ModelCache

#endif // SOLARSYSTEM_ASSETS_MODELCACHE_H
