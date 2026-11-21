#include "TerrainSystem.h"

#include "../../core/Log.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Terrain {

double HeightFade(double altitudeRadii) {
    const double t = std::clamp((altitudeRadii - kFadeFullRadii) / (kFadeZeroRadii - kFadeFullRadii), 0.0, 1.0);
    return 1.0 - t * t * (3.0 - 2.0 * t);
}

TerrainSystem::TerrainSystem(FetchFn fetch) : _cache(std::move(fetch)) {}

void TerrainSystem::Configure(const TerrainSettings& settings) {
    _settings = settings;
    _cache.SetLimits(settings.limits);
}

int TerrainSystem::AddBody(const std::string& id, double radiusKm) {
    const int existing = SlotOf(id);
    if (existing >= 0) {
        return existing;
    }
    Body body;
    body.id = id;
    body.radiusKm = radiusKm;
    _bodies.push_back(std::move(body));
    return static_cast<int>(_bodies.size()) - 1;
}

int TerrainSystem::SlotOf(const std::string& id) const {
    for (std::size_t i = 0; i < _bodies.size(); ++i) {
        if (_bodies[i].id == id) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void TerrainSystem::Update(const TerrainViewInput& view, const std::vector<TerrainBodyInput>& bodies) {
    _cache.BeginFrame();
    for (Body& body : _bodies) {
        body.wasActive = body.active;
        body.active = false;
        body.inReach = false;
    }
    if (_settings.enabled) {
        for (const TerrainBodyInput& input : bodies) {
            if (input.slot < 0 || input.slot >= static_cast<int>(_bodies.size())) {
                continue;
            }
            Body& body = _bodies[static_cast<std::size_t>(input.slot)];
            BodyFrame frame;
            frame.center = input.center;
            frame.shape = input.shape;
            frame.meshRadius = input.meshRadius;
            const double radiusWorld = input.meshRadius * frame.MaxScale();
            if (!(radiusWorld > 0.0)) {
                continue;
            }
            const double altitudeRadii = glm::distance(view.eye, input.center) / radiusWorld - 1.0;
            if (altitudeRadii < kPrefetchRadii) {
                _cache.Prepare(input.slot, body.id);
                body.inReach = _cache.State(input.slot) != TileCache::SetState::Missing;
            }
            const double threshold = body.wasActive ? kReleaseRadii : kEngageRadii;
            const TileSet* set = _cache.Set(input.slot);
            if (altitudeRadii >= threshold || !_cache.Ready(input.slot) || !set) {
                continue;
            }
            if (!body.wasActive) {
                LOG_DEBUG("Terrain", "%s: tiles on at %.3f radii", body.id.c_str(), altitudeRadii);
            }
            body.active = true;
            body.eye = view.eye;
            body.frame.fade = HeightFade(altitudeRadii);
            // Tile heights are metres above the catalog radius the mesh is drawn at.
            frame.heightScale = body.frame.fade * input.meshRadius / (body.radiusKm * 1000.0);
            frame.minHeightM = set->minHeightM;
            frame.maxHeightM = set->maxHeightM;
            const glm::dvec2 rangesFor(radiusWorld, view.pixelsPerRadian);
            if (rangesFor != body.rangesFor || _settings.maxPixelError != body.rangesError) {
                body.ranges = LevelRanges::Make(radiusWorld, view.pixelsPerRadian, _settings.maxPixelError);
                body.rangesFor = rangesFor;
                body.rangesError = _settings.maxPixelError;
            }
            SelectInput select;
            select.eye = view.eye;
            select.viewDirection = view.viewDirection;
            select.pixelsPerRadian = view.pixelsPerRadian;
            select.views = view.views;
            select.light = input.light;
            body.frame.frame = frame;
            Select(frame, body.ranges, select, _cache.Source(input.slot), body.frame.selection);
            body.drawn.clear();
            for (std::size_t i = 0; i < body.frame.selection.tiles.size(); ++i) {
                body.drawn.emplace(body.frame.selection.tiles[i].tile->key.Packed(), i);
            }
        }
    }
    for (Body& body : _bodies) {
        if (body.wasActive && !body.active) {
            body.frame.selection = Selection{};
            body.drawn.clear();
            LOG_DEBUG("Terrain", "%s: back to the sphere", body.id.c_str());
        }
    }
    _cache.EndFrame();
}

const ActiveBody* TerrainSystem::Active(int slot) const {
    if (slot < 0 || slot >= static_cast<int>(_bodies.size()) || !_bodies[static_cast<std::size_t>(slot)].active) {
        return nullptr;
    }
    return &_bodies[static_cast<std::size_t>(slot)].frame;
}

bool TerrainSystem::InReach(int slot) const {
    return slot >= 0 && slot < static_cast<int>(_bodies.size()) && _bodies[static_cast<std::size_t>(slot)].inReach;
}

std::optional<double> TerrainSystem::SurfaceRadius(int slot, const glm::dvec3& direction) const {
    const ActiveBody* active = Active(slot);
    if (!active || glm::length(direction) == 0.0) {
        return std::nullopt;
    }
    const Body& body = _bodies[static_cast<std::size_t>(slot)];
    const BodyFrame& frame = active->frame;
    const FacePoint point = FromDirection(direction);
    const auto grid = [&point](const TileKey& key) {
        const TileExtent e = Extent(key);
        return glm::dvec2((point.s - e.s0) / e.size, (point.t - e.t0) / e.size) * static_cast<double>(kTileQuads);
    };

    // The tile drawn there: its triangles, morphed as terrain.vs morphs them.
    for (int level = active->selection.deepest; level >= 0; --level) {
        const TileKey key = TileAt(point, level);
        const auto found = body.drawn.find(key.Packed());
        if (found == body.drawn.end()) {
            continue;
        }
        const DrawTile& draw = active->selection.tiles[found->second];
        const glm::dvec2 g = grid(key);
        double height = SurfaceHeight(*draw.tile, g.x, g.y);
        if (draw.parent) {
            const int q = key.Quadrant();
            const glm::dvec2 p = 0.5 * (g + static_cast<double>(kTileQuads) * glm::dvec2(q & 1, (q >> 1) & 1));
            const glm::dvec3 at = frame.center + frame.shape * (glm::normalize(direction) *
                                                               (frame.meshRadius + height * frame.heightScale));
            const double k = MorphWeight(draw.morph, glm::distance(at, body.eye));
            height += k * (SurfaceHeight(*draw.parent, p.x, p.y) - height);
        }
        return frame.meshRadius + height * frame.heightScale;
    }

    // Not drawn (out of every view): the deepest resident tile under it.
    const HeightTile* deepest = nullptr;
    for (int level = 0; level <= kMaxLevel; ++level) {
        const HeightTile* tile = _cache.Find(slot, TileAt(point, level));
        if (!tile) {
            break;
        }
        deepest = tile;
    }
    if (!deepest) {
        return std::nullopt;
    }
    const glm::dvec2 g = grid(deepest->key);
    return frame.meshRadius + SurfaceHeight(*deepest, g.x, g.y) * frame.heightScale;
}

std::string TerrainSystem::StatsLine() const {
    const CacheStats stats = _cache.Stats();
    int drawn = 0;
    std::string bodies;
    for (const Body& body : _bodies) {
        if (!body.active) {
            continue;
        }
        const Selection& selection = body.frame.selection;
        drawn += static_cast<int>(selection.tiles.size());
        char part[96];
        std::snprintf(part, sizeof(part), "%s%s %zu (level <= %d, fade %.2f)", bodies.empty() ? "" : ", ",
                      body.id.c_str(), selection.tiles.size(), selection.deepest, body.frame.fade);
        bodies += part;
    }
    char line[256];
    std::snprintf(line, sizeof(line),
                  "budget %.1f MiB, resident %d tiles (%.1f MiB, %d upsampled), drawn %d%s%s%s, in flight %d/%d, "
                  "fetched %d, missing %d",
                  static_cast<double>(_cache.Limits().budgetBytes) / (1024.0 * 1024.0), stats.resident,
                  static_cast<double>(stats.bytes) / (1024.0 * 1024.0), stats.synthesized, drawn,
                  bodies.empty() ? "" : " [", bodies.c_str(), bodies.empty() ? "" : "]", stats.inFlight,
                  _cache.Limits().maxInFlight, stats.requested, stats.failed);
    return line;
}

} // namespace Terrain
