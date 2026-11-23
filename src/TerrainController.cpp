// Terrain for the bodies flagged render.terrain (docs/ARCHITECTURE.md §11.3): the tile
// fetch hook, the per-frame decision of which of them draw tiles instead of their sphere,
// and the [Terrain] log line. The quadtree, cache and LOD are GL-free in src/render/terrain/;
// Renderer draws what Contribute puts in the frame snapshot.
#include "TerrainController.h"
#include "FrameContext.h"
#include "ApplicationTypes.h"
#include "IAppModule.h"
#include "ObserveState.h"
#include "QualitySettings.h"
#include "Renderer.h"
#include "SimState.h"
#include "Solar_System/BodyCatalog.h"
#include "Solar_System/SpaceObject.h"
#include "gfx/Frustum.h"
#include "assets/WebResourceFetcher.h"
#include "core/CameraNavigation.h"
#include "core/Log.h"
#include "render/terrain/TerrainSystem.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

// How often, at most, the [Terrain] line goes out while a body draws tiles (when it changed).
constexpr double kTerrainLogSeconds = 5.0;

std::optional<std::vector<uint8_t>> ReadBytes(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return std::nullopt;
    }
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

// Every tile goes through WebResourceFetcher, so on web the page's asset cache
// (web/src/assetCache.ts) answers a revisit, with the network off too. Natively the files
// are read from resource/terrain/ where the baker wrote them.
void FetchTerrainFile(const std::string& path, Terrain::FetchDone done) {
    WebResourceFetcher::DownloadFile(path, path, [path, done = std::move(done)](bool success) {
        std::optional<std::vector<uint8_t>> bytes = success ? ReadBytes(path) : std::nullopt;
#ifdef __EMSCRIPTEN__
        // The cache holds the bytes now; a copy in MEMFS would only grow the heap.
        if (success) {
            std::remove(path.c_str());
        }
#endif
        done(std::move(bytes));
    });
}

} // namespace

TerrainController::TerrainController(const ModuleContext& app, Renderer& renderer) : _app(app), _renderer(renderer) {}
TerrainController::~TerrainController() = default;

void TerrainController::Configure(const QualityTierSettings& settings) {
    if (!_terrain) {
        _terrain = std::make_unique<Terrain::TerrainSystem>(FetchTerrainFile);
    }
    Terrain::TerrainSettings terrain;
    terrain.enabled = settings.terrainBudgetMiB > 0;
    terrain.maxPixelError = settings.terrainPixelError;
    terrain.limits.budgetBytes = static_cast<std::size_t>(settings.terrainBudgetMiB) * 1024u * 1024u;
    terrain.limits.maxInFlight = settings.terrainMaxInFlight;
    _terrain->Configure(terrain);
}

int TerrainController::SlotOf(const SpaceObject& object) const {
    const BodyCatalog::Entry* entry = object.CatalogEntry();
    return _terrain && entry && entry->hasTerrain ? _terrain->SlotOf(entry->id) : -1;
}

std::optional<double> TerrainController::SurfaceRadius(const SpaceObject& object, const glm::dvec3& eye) const {
    const int slot = SlotOf(object);
    if (slot < 0) {
        return std::nullopt;
    }
    CameraNavigation::Body body;
    body.center = object.GetWorldPosition();
    body.shape = glm::dmat3(glm::mat3(object.GetModelMatrix()));
    return _terrain->SurfaceRadius(slot, CameraNavigation::ToModel(body, eye));
}

void TerrainController::Update(SimState& sim) {
    _selection.clear();
    if (!_terrain) {
        return;
    }
    const CameraView& head = _app.views[0];
    std::vector<Frustum> views;
    for (int view = 0; view < _app.viewCount; ++view) {
        const CameraView& camera = _app.views[static_cast<std::size_t>(view)];
        views.emplace_back(camera.projection * camera.view);
    }
    Terrain::TerrainViewInput view;
    view.eye = glm::dvec3(head.headPosition);
    view.viewDirection = glm::dvec3(head.front);
    view.pixelsPerRadian =
        std::max<double>(head.viewport.w, 1.0) / (2.0 * std::tan(glm::radians(static_cast<double>(head.zoomDeg)) * 0.5));
    view.views = &views;

    // Observe draws only the sky, so every body stays (and is drawn as) its sphere.
    std::vector<Terrain::TerrainBodyInput> bodies;
    std::vector<const SpaceObject*> objects;
    std::vector<Frustum> lights;
    lights.reserve(_app.components.size()); // bodies point into it
    for (const RenderableSceneComponent& component : _app.components) {
        if (_app.observe.active) {
            break;
        }
        const Frustum* light = nullptr;
        if (sim.shadowQuality > 0) {
            lights.emplace_back(component.lightSpaceMatrix);
            light = &lights.back();
        }
        const auto add = [&](const SpaceObject& object) {
            const BodyCatalog::Entry* entry = object.CatalogEntry();
            if (!entry || !entry->hasTerrain) {
                return;
            }
            Terrain::TerrainBodyInput input;
            input.slot = _terrain->AddBody(entry->id, entry->radiusKm);
            input.center = object.GetWorldPosition() - head.renderOrigin;
            input.shape = glm::dmat3(glm::mat3(object.GetModelMatrix()));
            input.meshRadius = object.GetModel().BoundingRadius();
            input.light = light;
            bodies.push_back(input);
            objects.push_back(&object);
        };
        add(*component.planet);
        for (const auto& satellite : component.satellites) {
            add(*satellite);
        }
    }
    _terrain->Update(view, bodies);

    bool anyActive = false;
    for (std::size_t i = 0; i < bodies.size(); ++i) {
        if (_terrain->InReach(bodies[i].slot)) {
            const Terrain::ActiveBody* tiles = _terrain->Active(bodies[i].slot);
            _selection.push_back({objects[i], tiles});
            anyActive = anyActive || tiles != nullptr;
        }
    }
    _renderer.ForgetTerrainTiles(_terrain->Cache().TakeEvicted());

    // The budget and what is resident, while any body draws tiles, and once more after.
    if (anyActive || !_logged.empty()) {
        char gpu[48];
        std::snprintf(gpu, sizeof(gpu), ", %zu on the GPU", _renderer.TerrainTextureCount());
        const std::string line = _terrain->StatsLine() + gpu;
        const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        if (!anyActive || (line != _logged && now - _loggedAt >= kTerrainLogSeconds)) {
            LOG_INFO("Terrain", "%s", line.c_str());
            _logged = anyActive ? line : std::string();
            _loggedAt = now;
        }
    }
}

void TerrainController::Contribute(SceneSnapshot& scene) {
    scene.terrain = _selection;
}
