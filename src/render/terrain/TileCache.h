#ifndef SOLARSYSTEM_RENDER_TERRAIN_TILECACHE_H
#define SOLARSYSTEM_RENDER_TERRAIN_TILECACHE_H
#include "TerrainQuadtree.h"
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

/**
 * Height tiles in memory for every terrain body (docs/ARCHITECTURE.md §11.3). GL-free:
 * TerrainRenderer keeps a GPU copy of each tile drawn and drops it when the tile leaves.
 *
 * One least-recently-used set under one byte budget (the quality tier's), counted as the
 * GPU copies cost. Tiles come from `resource/terrain/<body>/` through a FetchFn, which in
 * the app is WebResourceFetcher (so the page's asset cache answers a revisit with the
 * network off); a frame's requests go out in priority order up to an in-flight cap, and
 * the rest are asked for again next frame if they are still wanted. A tile the data set
 * does not have (no child bit, or a failed fetch) is upsampled from its parent instead.
 */
namespace Terrain {

struct TileSet {
    std::string body;           // catalog id: "moon"
    double radiusKm = 0.0;      // heights are metres above this sphere
    int maxLevel = 0;
    double minHeightM = 0.0;
    double maxHeightM = 0.0;
    uint8_t faces = 0;          // bit f: face f has tiles; the others are the sphere
    std::string attribution;
};

/** resource/terrain/<body>/tileset.json, which scripts/bake-terrain-tiles.py writes. */
std::optional<TileSet> ParseTileSet(const std::string& json, std::string* error = nullptr);

std::string TileSetPath(const std::string& body);
std::string TilePath(const std::string& body, const TileKey& key); // resource/terrain/<body>/<face>/<z>/<x>/<y>.ktx2

/** Fetch `path`; `done` gets its bytes, or nullopt when there are none. May call back at once or later. */
using FetchDone = std::function<void(std::optional<std::vector<uint8_t>>)>;
using FetchFn = std::function<void(const std::string& path, FetchDone done)>;

struct CacheLimits {
    std::size_t budgetBytes = 0;
    int maxInFlight = 0;
    int maxSynthesizedPerFrame = 128; // bounds the upsampling one frame can cost
};

struct CacheStats {
    int resident = 0;     // tiles in memory, synthesized included
    int synthesized = 0;
    std::size_t bytes = 0;
    int inFlight = 0;
    int requested = 0;    // fetches started, all time
    int failed = 0;       // fetches that found no tile, all time
    int evicted = 0;      // all time
};

class TileCache {
public:
    enum class SetState { Idle, Loading, Ready, Missing };

    explicit TileCache(FetchFn fetch);
    TileCache(const TileCache&) = delete;
    TileCache& operator=(const TileCache&) = delete;

    void SetLimits(const CacheLimits& limits) { _limits = limits; }
    const CacheLimits& Limits() const { return _limits; }

    /** Take what arrived since the last frame. Before any Select. */
    void BeginFrame();
    /** Start this frame's fetches, best first, and evict down to the budget. After the last Select. */
    void EndFrame();

    /** Body `slot` (an index the caller keeps per body): start loading its tile set the first time. */
    SetState Prepare(int slot, const std::string& body);
    SetState State(int slot) const;
    const TileSet* Set(int slot) const;
    /** Every root of the slot's body resident: selection can draw the whole sphere. */
    bool Ready(int slot) const;

    TileSource& Source(int slot);
    /** A resident tile, or nullptr; does not count as a use. */
    const HeightTile* Find(int slot, const TileKey& key) const;

    /** uids of tiles evicted since the last call: their GPU copies can go. */
    std::vector<uint64_t> TakeEvicted();
    CacheStats Stats() const;

private:
    struct Entry {
        HeightTile tile;
        uint64_t lastUsed = 0;
        bool pinned = false;
    };
    struct Body {
        std::string name;
        SetState state = SetState::Idle;
        std::optional<TileSet> set;
        int rootsResident = 0;
        std::unique_ptr<TileSource> source;
    };
    struct Request {
        int slot = 0;
        TileKey key;
        RequestPriority priority;
    };
    struct Arrival {
        int slot = 0;
        bool isSet = false;
        TileKey key;
        std::optional<std::vector<uint8_t>> bytes;
    };
    class SlotSource;

    static uint64_t Id(int slot, const TileKey& key) {
        return (static_cast<uint64_t>(slot) << 56) | key.Packed();
    }
    Body& BodyAt(int slot);
    const HeightTile* Insert(int slot, HeightTile tile, bool pinned);
    void Want(int slot, const TileKey& key, const RequestPriority& priority);
    void Fetch(const Request& request);
    void Arrive(Arrival arrival);
    void Evict();

    FetchFn _fetch;
    CacheLimits _limits;
    std::unordered_map<int, Body> _bodies;
    std::unordered_map<uint64_t, std::unique_ptr<Entry>> _entries;
    std::unordered_set<uint64_t> _absent;
    std::unordered_set<uint64_t> _inFlight;
    std::unordered_map<uint64_t, Request> _wanted;
    std::shared_ptr<std::deque<Arrival>> _arrivals = std::make_shared<std::deque<Arrival>>();
    std::vector<uint64_t> _evicted;
    uint64_t _frame = 1;
    uint64_t _nextUid = 1;
    int _synthesizedThisFrame = 0;
    int _requested = 0;
    int _failed = 0;
    int _evictedCount = 0;
};

} // namespace Terrain

#endif // SOLARSYSTEM_RENDER_TERRAIN_TILECACHE_H
