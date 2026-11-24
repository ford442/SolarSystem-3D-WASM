// What web timelines and the page ask of the scene between frames (docs/ARCHITECTURE.md §11.2):
// the body catalog, every body's placement at the current date, where a surface is, system
// preloads, the HUD mask and pending assets. The timeline itself is evaluated in TypeScript
// (web/src/timeline.ts); fixed-step capture is the Cinematics module (Cinematics.h).
#include "Application.h"
#include "Renderer.h"
#include "Solar_System/SpaceObject.h"
#include "Solar_System/BodyCatalog.h"
#include "Solar_System/CatalogSatellite.h"
#include "Solar_System/OrbitLayout.h"
#include "Solar_System/SceneScale.h"
#include "Solar_System/StarSystemCatalog.h"
#include "FlightController.h"
#include "assets/TextureLoadingQueue.h"
#include <algorithm>
#include <cstdio>
#include <memory>
#include <cstdlib>
#include <string>

namespace {

/** Catalog display names are ASCII; anything else (and JSON's two specials) is dropped. */
std::string AsciiName(const wchar_t* name) {
    std::string out;
    for (const wchar_t* c = name; c && *c; ++c) {
        if (*c > 0 && *c < 128 && *c != L'"' && *c != L'\\') {
            out += static_cast<char>(*c);
        }
    }
    return out;
}

const char* KindSlug(BodyCatalog::Kind kind) {
    switch (kind) {
        case BodyCatalog::Kind::Star: return "star";
        case BodyCatalog::Kind::Planet: return "planet";
        case BodyCatalog::Kind::DwarfPlanet: return "dwarf-planet";
        case BodyCatalog::Kind::Satellite: return "satellite";
        case BodyCatalog::Kind::Comet: return "comet";
    }
    return "planet";
}

int MaxCatalogIndex() {
    int maxIndex = 0;
    for (const BodyCatalog::Entry& entry : BodyCatalog::Entries()) {
        maxIndex = std::max(maxIndex, entry.index);
    }
    return maxIndex;
}

/** Drawn equatorial radius of a loaded body: the mesh's radius times its model scale. */
double DrawnRadius(const SpaceObject& object) {
    return static_cast<double>(object.GetModel().BoundingRadius()) *
           static_cast<double>(glm::length(glm::vec3(object.GetModelMatrix()[0])));
}

} // namespace

int Application::PendingAssetCount() const {
    int pending = TextureLoadingQueue::GetInstance().GetQueuedCount() + _resourcesPending.load();
#ifdef __EMSCRIPTEN__
    for (const auto& manifest : _planetSystemManifests) {
        if (manifest.state == PlanetSystemManifest::State::DOWNLOADING) {
            ++pending;
        }
    }
#endif
    return pending;
}

std::shared_ptr<const SpaceObject> Application::FindSceneBody(int catalogIndex) const {
    if (catalogIndex == 0) {
        return _sun;
    }
    const BodyCatalog::Entry* entry = BodyCatalog::FindByIndex(catalogIndex);
    if (!entry) {
        return nullptr;
    }
    for (const auto& component : _renderableSceneComponents) {
        if (entry->kind != BodyCatalog::Kind::Satellite) {
            // Primaries by name, as FocusPlanetByIndex: catalog index = OrbitLayout::Body.
            if (component.planet && catalogIndex < OrbitLayout::BodyCount()) {
                const std::wstring& wideName = component.planet->GetEngName();
                if (OrbitLayout::BodyFromName(std::string(wideName.begin(), wideName.end())) ==
                    static_cast<OrbitLayout::Body>(catalogIndex)) {
                    return component.planet;
                }
            }
            continue;
        }
        for (const auto& satellite : component.satellites) {
            const auto* catalogSatellite = dynamic_cast<const CatalogSatellite*>(satellite.get());
            if (catalogSatellite && catalogSatellite->GetEntry().index == catalogIndex) {
                return satellite;
            }
        }
    }
    return nullptr;
}

std::string Application::GetBodyCatalogJson() const {
    const BodyCatalog::StarRow& star = BodyCatalog::Star();
    std::string json = std::string(R"([{"index":0,"id":")") + star.id + R"(","name":")" +
                       AsciiName(star.displayNameEn) + R"(","parent":null,"kind":"star","radiusKm":)" +
                       std::to_string(star.radiusKm) + "}";
    char buffer[256];
    for (const BodyCatalog::Entry& entry : BodyCatalog::Entries()) {
        const std::string parent = entry.parentId ? std::string("\"") + entry.parentId + "\"" : "null";
        std::snprintf(buffer, sizeof(buffer),
                      R"(,{"index":%d,"id":"%s","name":"%s","parent":%s,"kind":"%s","radiusKm":%.3f})", entry.index,
                      entry.id, AsciiName(entry.displayNameEn).c_str(), parent.c_str(), KindSlug(entry.kind),
                      static_cast<double>(entry.radiusKm));
        json += buffer;
    }
    json += ']';
    return json;
}

std::string Application::GetSceneStateJson() {
    // The date may have moved since the frame (a timeline sets it, then asks where things are).
    PlaceBodies();
    std::string json;
    json.reserve(4096);
    char buffer[192];
    std::snprintf(buffer, sizeof(buffer), R"({"jd":%.10f,"scale":%d,"bodies":[)", OrbitLayout::GetJulianDate(),
                  static_cast<int>(OrbitLayout::GetScaleMode()));
    json += buffer;
    const int maxIndex = MaxCatalogIndex();
    for (int index = 0; index <= maxIndex; ++index) {
        if (index > 0) {
            json += ',';
        }
        glm::dvec3 position(0.0);
        double radius = 0.0;
        if (const std::shared_ptr<const SpaceObject> object = FindSceneBody(index)) {
            position = object->GetWorldPosition();
            radius = DrawnRadius(*object);
        } else if (const BodyCatalog::Entry* entry = BodyCatalog::FindByIndex(index);
                   entry && entry->kind != BodyCatalog::Kind::Satellite && index < OrbitLayout::BodyCount()) {
            // Not loaded yet (web staged loading): where the ephemeris puts it.
            position = OrbitLayout::GetHeliocentricScenePosition(static_cast<OrbitLayout::Body>(index));
            radius = SceneScale::BodyRadius(*entry);
        } else {
            json += "null"; // an unloaded moon has no placement until its system arrives
            continue;
        }
        // %.17g: a metre on realistic scale is the 13th significant digit of a position.
        std::snprintf(buffer, sizeof(buffer), "[%.17g,%.17g,%.17g,%.9g]", position.x, position.y, position.z, radius);
        json += buffer;
    }
    json += R"(],"missions":[)";
    for (int i = 0; i < _missions.Count(); ++i) {
        if (i > 0) {
            json += ',';
        }
        glm::dvec3 probe(0.0);
        if (!_missions.SampleHeliocentric(i, probe)) {
            json += "null";
            continue;
        }
        std::snprintf(buffer, sizeof(buffer), "[%.17g,%.17g,%.17g]", probe.x, probe.y, probe.z);
        json += buffer;
    }
    // The flight planner's craft at this date (a timeline's `focus: "flight"`), or null.
    json += R"(],"flight":)";
    if (_flight.HasFlight() && StarSystemCatalog::IsSol()) {
        const glm::dvec3 craft = _flight.Craft().WorldPositionAt(OrbitLayout::GetJulianDate());
        std::snprintf(buffer, sizeof(buffer), "[%.17g,%.17g,%.17g]", craft.x, craft.y, craft.z);
        json += buffer;
    } else {
        json += "null";
    }
    json += "}";
    return json;
}

void Application::PreloadBodySystem(int catalogIndex) {
    const BodyCatalog::Entry* entry = BodyCatalog::FindByIndex(catalogIndex);
    if (entry && entry->kind == BodyCatalog::Kind::Satellite && entry->parentId) {
        entry = BodyCatalog::FindById(entry->parentId);
    }
    if (entry && entry->index > 0 && entry->index < OrbitLayout::BodyCount()) {
        ActivatePlanetSystem(static_cast<OrbitLayout::Body>(entry->index));
    }
}

void Application::SetHudOverlays(int mask) {
    Renderer::OverlayToggles& toggles = _renderer.GetOverlayToggles();
    toggles.hints = (mask & 1) != 0;
    toggles.planetStarDistances = (mask & 2) != 0;
    toggles.satelliteDistances = (mask & 4) != 0;
    toggles.stagingLabels = (mask & 8) != 0;
}

int Application::GetHudOverlays() const {
    const Renderer::OverlayToggles& toggles = _renderer.GetOverlayToggles();
    return (toggles.hints ? 1 : 0) | (toggles.planetStarDistances ? 2 : 0) | (toggles.satelliteDistances ? 4 : 0) |
           (toggles.stagingLabels ? 8 : 0);
}

Application::SurfacePoint Application::SurfaceAlong(int catalogIndex, const glm::dvec3& direction) const {
    const std::shared_ptr<const SpaceObject> object = FindSceneBody(catalogIndex);
    if (object) {
        return _cameraController.SurfaceOf(*object, direction);
    }
    // Not loaded yet: its analytic sphere, with the direction as the normal.
    const double length = glm::length(direction);
    const glm::dvec3 radial = length > 0.0 ? direction / length : glm::dvec3(1.0, 0.0, 0.0);
    const BodyCatalog::Entry* entry = BodyCatalog::FindByIndex(catalogIndex);
    return {catalogIndex == 0 ? SceneScale::SunRadius() : entry ? SceneScale::BodyRadius(*entry) : 0.0, radial};
}
