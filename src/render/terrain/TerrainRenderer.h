#ifndef SOLARSYSTEM_RENDER_TERRAIN_TERRAINRENDERER_H
#define SOLARSYSTEM_RENDER_TERRAIN_TERRAINRENDERER_H
#include "TerrainSystem.h"
#include "../../gfx/GlHandle.h"
#include <cstdint>
#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>

class Shader;

/**
 * The GPU side of terrain (docs/ARCHITECTURE.md §11.3): one shared 65×65 grid with skirts,
 * and an R32F texture per tile drawn, made the first time a pass draws it and deleted when
 * the cache evicts the tile. The colour and shadow passes bind their program (terrain.vs
 * with terrain.fs or shadowMap.fs) and the body's material, then call Draw.
 */
class TerrainRenderer {
public:
    /** Which of a selection's tiles a call draws. */
    enum class Pass {
        ColourFar,  // in a view, vertex log depth
        ColourNear, // in a view, close enough for the NEAR_SURFACE program
        Shadow,     // in the component's light box
    };

    /** Draw `body`'s tiles for `pass` with `shader`, nearest first. Returns how many it drew. */
    int Draw(const Shader& shader, const Terrain::ActiveBody& body, const glm::mat4& model, Pass pass);
    /** Delete the GPU copies of tiles the cache evicted (TileCache::TakeEvicted). */
    void Forget(const std::vector<uint64_t>& uids);
    std::size_t TextureCount() const { return _textures.size(); }

private:
    void BuildGrid();
    GLuint TextureFor(const Terrain::HeightTile& tile);

    GlVertexArray _vao;
    GlBuffer _vertices;
    GlBuffer _indices;
    int _indexCount = 0;
    std::unordered_map<uint64_t, GlTexture> _textures; // by HeightTile::uid
};

#endif // SOLARSYSTEM_RENDER_TERRAIN_TERRAINRENDERER_H
