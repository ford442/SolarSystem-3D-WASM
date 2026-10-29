#ifndef SOLARSYSTEM_RENDER_TERRAIN_TERRAINSYSTEM_H
#define SOLARSYSTEM_RENDER_TERRAIN_TERRAINSYSTEM_H
#include "TileCache.h"
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * When each terrain body draws tiles instead of its sphere, and what the camera floor stands
 * on (docs/ARCHITECTURE.md §11.3). GL-free; Application owns it, Renderer draws its
 * selections.
 *
 * A body flagged render.terrain loads its tile set once the camera is within
 * kPrefetchRadii of altitude, swaps the sphere for tiles below kEngageRadii (back above
 * kReleaseRadii), and grows its heights from zero over the fade band, so the swap itself
 * happens on the bare analytic sphere under the sphere's own material. With terrain off
 * (the Low tier) nothing is ever fetched.
 */
namespace Terrain {

/** Altitudes in body radii. */
constexpr double kPrefetchRadii = 2.0;
constexpr double kEngageRadii = 0.5;
constexpr double kReleaseRadii = 0.55;
constexpr double kFadeFullRadii = 0.2;  // heights at full scale below this
constexpr double kFadeZeroRadii = 0.45; // and zero above this

/** The relief's weight at `altitudeRadii`: 1 low down, 0 from kFadeZeroRadii up. */
double HeightFade(double altitudeRadii);

struct TerrainSettings {
    bool enabled = false;       // false: every body is its sphere, and nothing is fetched
    double maxPixelError = 4.0; // quad size in pixels at the split distance
    CacheLimits limits;
};

/** One terrain body this frame (render frame). */
struct TerrainBodyInput {
    int slot = -1;
    glm::dvec3 center{0.0};
    glm::dmat3 shape{1.0};
    double meshRadius = 1.0;      // the sphere mesh's radius, model units
    const Frustum* light = nullptr; // its component's shadow box, when there is a shadow pass
};

struct TerrainViewInput {
    glm::dvec3 eye{0.0};
    glm::dvec3 viewDirection{0.0, 0.0, -1.0};
    double pixelsPerRadian = 1000.0;
    const std::vector<Frustum>* views = nullptr;
};

struct ActiveBody {
    BodyFrame frame;
    double fade = 0.0;
    Selection selection;
};

class TerrainSystem {
public:
    explicit TerrainSystem(FetchFn fetch);

    void Configure(const TerrainSettings& settings);
    const TerrainSettings& Settings() const { return _settings; }

    /** A body flagged render.terrain (catalog id, radius in km); returns the slot Update takes. */
    int AddBody(const std::string& id, double radiusKm);
    int SlotOf(const std::string& id) const;

    /** Decide and select for this frame. Selections point into the cache until the next Update. */
    void Update(const TerrainViewInput& view, const std::vector<TerrainBodyInput>& bodies);

    /** The body's tiles this frame, or nullptr when it draws as the sphere. */
    const ActiveBody* Active(int slot) const;
    /** Within kPrefetchRadii this frame (terrain on): its tile set is loading or loaded. */
    bool InReach(int slot) const;

    /**
     * Radius of the surface drawn under model direction `direction`, model units: the tile
     * there (the drawn one, else the deepest resident), morph and fade included. nullopt
     * when the body draws as its sphere or no tile under the direction is resident.
     */
    std::optional<double> SurfaceRadius(int slot, const glm::dvec3& direction) const;

    TileCache& Cache() { return _cache; }
    const TileCache& Cache() const { return _cache; }
    /** One line for the log: budget, resident, drawn, in flight. */
    std::string StatsLine() const;

private:
    struct Body {
        std::string id;
        double radiusKm = 0.0;
        bool active = false;
        bool wasActive = false;
        bool inReach = false;
        glm::dvec3 eye{0.0}; // the frame's LOD eye, for the morph under a surface query
        ActiveBody frame;
        std::unordered_map<uint64_t, std::size_t> drawn; // TileKey::Packed -> selection index
        LevelRanges ranges;
        glm::dvec2 rangesFor{-1.0}; // (radius world, pixels per radian) the ranges were made for
        double rangesError = -1.0;
    };

    TileCache _cache;
    TerrainSettings _settings;
    std::vector<Body> _bodies;
};

} // namespace Terrain

#endif // SOLARSYSTEM_RENDER_TERRAIN_TERRAINSYSTEM_H
