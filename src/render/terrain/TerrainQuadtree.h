#ifndef SOLARSYSTEM_RENDER_TERRAIN_TERRAINQUADTREE_H
#define SOLARSYSTEM_RENDER_TERRAIN_TERRAINQUADTREE_H
#include "TerrainTile.h"
#include "../../gfx/Frustum.h"
#include <array>
#include <glm/glm.hpp>
#include <vector>

/**
 * Which tiles of one body to draw this frame (docs/ARCHITECTURE.md §11.3). GL-free and
 * deterministic: the same body, camera and resident tiles give the same list.
 *
 * Every level has one split distance, a fixed multiple of its tiles' diagonal chosen so a
 * quad is at most the pixel error across at that distance (CDLOD). A node splits while its
 * bounding sphere is nearer than its level's distance and all four children are to hand;
 * otherwise it is drawn, and a child that is missing is asked for. Tiles at level L morph
 * their heights to level L-1's surface over [split(L) + diagonal(L), split(L-1)], so a tile
 * is entirely its parent's surface at the distance it splits or merges at (no pop), and
 * along an edge it shares with a coarser neighbour (no crack beyond what the skirts hide).
 *
 * Distances are in the render frame (world units), taken from the vertices the shader
 * draws, so terrain.vs and this agree on every vertex's morph.
 */
namespace Terrain {

/**
 * Split distance over tile diagonal at least this. A level's morph zone is
 * [ratio + 1, 2 ratio] diagonals, so the ratio has to stay above 1; at 1.5 the zone is the
 * outer sixth of the level's range, and a ring of tiles per level is about 3π ratio² × 2.
 */
constexpr double kMinSplitRatio = 1.5;

struct LevelRanges {
    std::array<double, kMaxLevel + 1> diagonal{}; // longest tile diagonal at each level, world
    std::array<double, kMaxLevel + 1> split{};    // a node whose bound is nearer than this splits
    double ratio = kMinSplitRatio;                // split / diagonal

    /**
     * `radiusWorld` the analytic sphere's largest world radius, `pixelsPerRadian` the view's
     * height / (2 tan(fovY / 2)), `maxPixelError` how many pixels a quad may span at the
     * split distance.
     */
    static LevelRanges Make(double radiusWorld, double pixelsPerRadian, double maxPixelError);

    /** Tiles at `level` morph to level - 1 over [x, y] (world); level 0 never does (x > y). */
    glm::dvec2 Morph(int level) const;
};

/** One terrain body this frame, in the render frame. */
struct BodyFrame {
    glm::dvec3 center{0.0};      // body centre
    glm::dmat3 shape{1.0};       // model offsets to render-frame offsets (the model matrix's 3×3)
    double meshRadius = 1.0;     // analytic sphere, model units
    double heightScale = 0.0;    // model units per metre of height, the engage fade included
    double minHeightM = 0.0;     // the tile set's lowest and highest sample
    double maxHeightM = 0.0;

    double MaxScale() const;     // longest column of shape: model to world, upper bound
    double MinScale() const;     // shortest: lower bound
};

/** Larger error first, then nearer the middle of the view. */
struct RequestPriority {
    int errorBucket = 0;     // floor(log2(pixels a quad of the drawn stand-in spans))
    double viewAngle = 0.0;  // radians between the view direction and the tile
    bool operator<(const RequestPriority& other) const {
        return errorBucket != other.errorBucket ? errorBucket > other.errorBucket : viewAngle < other.viewAngle;
    }
};

/** Where selection gets tiles: Terrain::TileCache, or a test double. */
class TileSource {
public:
    virtual ~TileSource() = default;
    /** Level-0 tile of `face`, or nullptr while it is not resident. */
    virtual const HeightTile* Root(int face) = 0;
    /**
     * Quadrant `quadrant` of `parent`: its own tile when the data has one (nullptr until it
     * arrives, fetched in `priority` order), else upsampled from the parent.
     */
    virtual const HeightTile* Child(const HeightTile& parent, int quadrant, const RequestPriority& priority) = 0;
    /** `tile` is drawn this frame, or is an ancestor of one: keep it resident. */
    virtual void Touch(const HeightTile& tile) = 0;
};

struct SelectInput {
    glm::dvec3 eye{0.0};                     // the LOD eye, render frame: the camera (the origin)
    glm::dvec3 viewDirection{0.0, 0.0, -1.0};
    double pixelsPerRadian = 1000.0;
    const std::vector<Frustum>* views = nullptr; // colour views; nullptr: everything is in view
    const Frustum* light = nullptr;              // the shadow pass's box; nullptr: no shadow pass
};

/** A tile to draw, with what terrain.vs is given for it. */
struct DrawTile {
    const HeightTile* tile = nullptr;
    const HeightTile* parent = nullptr; // what it morphs to; nullptr at level 0
    TileFrame frame;
    glm::dvec3 center{0.0};  // frame.centerModel in the render frame
    glm::dvec2 morph{1.0, 0.0};
    double distance = 0.0;   // eye to the tile's bound, world
    double skirtDepth = 0.0; // model units
    bool inView = true;
    bool inLight = false;
    bool nearSurface = false; // nearer than its own diagonal: the NEAR_SURFACE (fragment depth) program
};

struct Selection {
    std::vector<DrawTile> tiles; // nearest first
    int horizonCulled = 0;
    int frustumCulled = 0;
    int waiting = 0;   // nodes drawn coarser than wanted while a child loads
    int deepest = 0;
};

/** Model-space bound of a tile at the body's heights; skirts included. */
BoundingSphere TileBound(const TileKey& key, const HeightTile& tile, const BodyFrame& body, double skirtDepth);

/** Skirt hanging below the tile's edges, model units: enough to hide a level step. */
double SkirtDepth(const TileKey& key, const HeightTile& tile, const BodyFrame& body);

/**
 * Whether the sphere `bound` (render frame) is entirely behind the body's horizon as seen
 * from `eye`, the body standing in as a sphere of `occluderRadius` about `center`.
 */
bool BehindHorizon(const glm::dvec3& eye, const glm::dvec3& center, double occluderRadius, const BoundingSphere& bound);

void Select(const BodyFrame& body, const LevelRanges& ranges, const SelectInput& input, TileSource& source,
            Selection& out);

/**
 * terrain.vs's morph weight for a vertex `distance` (world) from the eye: 0 its own
 * heights, 1 its parent's surface.
 */
inline double MorphWeight(const glm::dvec2& morph, double distance) {
    if (morph.x >= morph.y) {
        return 0.0;
    }
    const double k = (distance - morph.x) / (morph.y - morph.x);
    return k < 0.0 ? 0.0 : (k > 1.0 ? 1.0 : k);
}

/** The height a level-L tile's grid point (gx, gy) shows at morph `k`, metres: terrain.vs's blend. */
double MorphedHeight(const HeightTile& tile, const HeightTile* parent, int gx, int gy, double k);

/** Parent's surface under child grid point (gx, gy), metres: what a fully morphed vertex shows. */
double ParentSurfaceHeight(const HeightTile& parent, int quadrant, int gx, int gy);

} // namespace Terrain

#endif // SOLARSYSTEM_RENDER_TERRAIN_TERRAINQUADTREE_H
