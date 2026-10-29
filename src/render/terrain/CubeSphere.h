#ifndef SOLARSYSTEM_RENDER_TERRAIN_CUBESPHERE_H
#define SOLARSYSTEM_RENDER_TERRAIN_CUBESPHERE_H
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>

/**
 * The cube-sphere a terrain body is tiled on (docs/ARCHITECTURE.md §11.3). GL-free; the
 * vertex math here is what resource/shaders/terrain.vs does, so the tests check the shader.
 *
 * Model space is the body mesh's: +Y is the pole and a map longitude is -atan2(z, x), the
 * convention OrbitLayout's kEarthTextureCentreLonDeg documents. Each cube face has a basis
 * (n, u, v) with u × v = n, face coordinates s, t ∈ [-1, 1], and the equal-angle warp
 * w = tan(π/4 · s) puts the cube point at n + w(s)·u + w(t)·v, so tiles stay within ~1.4× of
 * one another in size from the face centre to its corners.
 *
 * A tile at level L covers a 2^-L square of its face with kTileQuads² quads; its heights are
 * kTileSamples² samples on the quad corners plus a one-sample apron for the normals.
 */
namespace Terrain {

constexpr int kTileQuads = 64;
constexpr int kTileSamples = kTileQuads + 1;
constexpr int kTileApron = 1;
constexpr int kTileStride = kTileSamples + 2 * kTileApron; // samples per row, apron included
constexpr int kFaceCount = 6;
/** Deepest level a tile can have, data or not: 2^-24 of Earth's face is 0.6 m. */
constexpr int kMaxLevel = 24;

struct FaceBasis {
    glm::dvec3 n, u, v;
};
/** Faces +X, -X, +Y, -Y, +Z, -Z. */
FaceBasis Basis(int face);

struct TileKey {
    uint8_t face = 0;
    uint8_t level = 0;
    uint32_t x = 0, y = 0; // column along u, row along v; each < 2^level

    /** Unique per tile: face, level, x, y in 3 + 5 + 24 + 24 bits. */
    uint64_t Packed() const {
        return (static_cast<uint64_t>(face) << 53) | (static_cast<uint64_t>(level) << 48) |
               (static_cast<uint64_t>(x) << 24) | static_cast<uint64_t>(y);
    }
    TileKey Parent() const { return {face, static_cast<uint8_t>(level - 1), x >> 1, y >> 1}; }
    /** Quadrant q of this tile: bit 0 the u half, bit 1 the v half. */
    TileKey Child(int q) const {
        return {face, static_cast<uint8_t>(level + 1), (x << 1) | (q & 1u), (y << 1) | ((q >> 1) & 1u)};
    }
    /** Which quadrant of its parent this tile is. */
    int Quadrant() const { return static_cast<int>((x & 1u) | ((y & 1u) << 1)); }
    bool operator==(const TileKey& other) const { return Packed() == other.Packed(); }
};

/** The tile's square in face coordinates: [s0, s0 + size] × [t0, t0 + size]. */
struct TileExtent {
    double s0, t0, size;
};
TileExtent Extent(const TileKey& key);

inline double Warp(double s) {
    return std::tan(0.25 * 3.14159265358979323846 * s);
}

/** Unit model-space direction of face point (s, t). */
glm::dvec3 Direction(int face, double s, double t);

struct FacePoint {
    int face;
    double s, t;
};
/** The face and face coordinates a model-space direction (any length) passes through. */
FacePoint FromDirection(const glm::dvec3& direction);

/** The deepest tile at `level` containing a face point. */
TileKey TileAt(const FacePoint& point, int level);

/**
 * Per-tile values terrain.vs is given (float), and the double point they are relative to.
 *
 * A vertex is placed as an offset from the tile's centre point, not as a radius times a
 * direction: that product is 1700 km on the Moon and a float there is spaced 0.1 m. The warp
 * difference uses tan(a + b) - tan(a) = sin b / (cos a · cos(a + b)), and the reference cube
 * point is the float-rounded one (warpCorrection carries the rounding), so every term is
 * relative to the offset and a vertex lands to ~1e-7 of the tile's size.
 */
struct TileFrame {
    glm::vec2 centerWarp{0.0f};     // float(w(s_c)), float(w(t_c))
    glm::vec2 warpCorrection{0.0f}; // w(s_c) - centerWarp, taken in double
    glm::vec2 centerCos{1.0f};      // cos(π/4 · s_c), cos(π/4 · t_c)
    glm::vec2 centerSin{0.0f};
    float size = 2.0f;              // tile edge in face coordinates
    /** Model-space centre on the analytic sphere: meshRadius · normalize(n + centerWarp · (u, v)). */
    glm::dvec3 centerModel{0.0};
};
TileFrame MakeTileFrame(const TileKey& key, double meshRadius);

/**
 * terrain.vs's vertex placement, in float: the model-space offset from `frame.centerModel`
 * of grid point `grid` (0..kTileQuads on each axis, fractional while it geomorphs) raised
 * `heightModel` model units along its direction.
 */
glm::vec3 GridOffset(const TileFrame& frame, int face, glm::vec2 grid, float meshRadius, float heightModel);

/** The same point in double, straight from the face coordinates: the reference the float path is held to. */
glm::dvec3 GridPointExact(const TileKey& key, double gx, double gy, double meshRadius, double heightModel);

} // namespace Terrain

#endif // SOLARSYSTEM_RENDER_TERRAIN_CUBESPHERE_H
