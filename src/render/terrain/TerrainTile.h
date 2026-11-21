#ifndef SOLARSYSTEM_RENDER_TERRAIN_TERRAINTILE_H
#define SOLARSYSTEM_RENDER_TERRAIN_TERRAINTILE_H
#include "CubeSphere.h"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

/**
 * One tile's heights (docs/ARCHITECTURE.md §11.3). GL-free.
 *
 * On disk a tile is a KTX2 of VK_FORMAT_R16_UNORM, kTileStride × kTileStride texels, rows in
 * increasing t from the apron row below the tile (KTXorientation "ru"), written by
 * scripts/bake-terrain-tiles.py. The key/value entry "SSterrain" holds
 * "minM=<metres> maxM=<metres> children=<mask>": a texel v is minM + (maxM - minM)·v/65535
 * metres above the catalog radius, and bit q of the mask says quadrant q has a tile of its
 * own. Past the data, a child is upsampled from its parent (Synthesized) so the mesh under a
 * camera metres up still refines; it adds no detail.
 */
namespace Terrain {

struct HeightTile {
    TileKey key;
    std::vector<float> heights; // kTileStride² metres, apron included, row = j + kTileApron
    float minHeight = 0.0f;     // over every sample, apron included
    float maxHeight = 0.0f;
    uint8_t childMask = 0;
    bool synthesized = false;
    uint64_t uid = 0; // unique per load, so a GPU copy can tell a reloaded tile from the old one

    /** Height at grid sample (gx, gy), each in [-kTileApron, kTileQuads + kTileApron]. */
    float At(int gx, int gy) const {
        return heights[static_cast<std::size_t>((gy + kTileApron) * kTileStride + gx + kTileApron)];
    }
};

constexpr uint32_t kVkFormatR16Unorm = 70;
constexpr std::size_t kTileGpuBytes = static_cast<std::size_t>(kTileStride) * kTileStride * sizeof(float);

/** Decode a tile file; nullopt (with `error`) for anything that is not a well-formed tile. */
std::optional<HeightTile> ParseHeightTile(const uint8_t* data, std::size_t size, const TileKey& key,
                                          std::string* error = nullptr);

/** The "SSterrain" value ParseHeightTile reads: "minM=… maxM=… children=…". */
std::string TileMetadata(float minM, float maxM, uint8_t childMask);

/** A tile with every sample at `heightM` (a face the data does not cover is the sphere). */
HeightTile FlatTile(const TileKey& key, float heightM = 0.0f);

/**
 * Child `quadrant` of `parent` by Catmull-Rom on the parent's samples: its even samples are
 * the parent's own, so a split geomorphs without a jump, and two children of neighbouring
 * parents agree along the edge they share.
 */
HeightTile UpsampleChild(const HeightTile& parent, int quadrant);

/**
 * Height of the surface the tile draws at grid point (gx, gy) ∈ [0, kTileQuads]²: the two
 * triangles of each quad as the index buffer splits it (along (i, j)–(i+1, j+1)).
 */
double SurfaceHeight(const HeightTile& tile, double gx, double gy);

} // namespace Terrain

#endif // SOLARSYSTEM_RENDER_TERRAIN_TERRAINTILE_H
