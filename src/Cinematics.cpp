// Engine side of web timelines and video capture (docs/ARCHITECTURE.md §11.2): fixed-step
// capture frames, the scene state a timeline places its camera from, and the HUD and
// preload switches a cinematic needs. The timeline itself is evaluated in TypeScript
// (web/src/timeline.ts); everything here is a query or a setter it calls between frames.
#include "Application.h"
#include "Solar_System/BodyCatalog.generated.h"
#include "Solar_System/CatalogSatellite.h"
#include "Solar_System/SceneScale.h"
#include "assets/TextureLoadingQueue.h"
#include "core/Log.h"
#include <algorithm>
#include <cstdio>
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
    for (const BodyCatalog::Entry& entry : BodyCatalog::kEntries) {
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

void Application::BeginCapture(int width, int height) {
#ifdef __EMSCRIPTEN__
    if (_xr.active) {
        LOG_WARN("Capture", "Not started: a WebXR session is presenting");
        return;
    }
#endif
    _capture.active = true;
    _capture.stepSeconds = 0.0;
    _simState.animationSeconds = 0.0;
#ifdef __EMSCRIPTEN__
    if (width > 0 && height > 0) {
        _capture.resized = true;
        _capture.width = width;
        _capture.height = height;
        emscripten_set_canvas_element_size("#canvas", width, height);
        HandleResize(width, height);
    }
#else
    (void)width;
    (void)height;
#endif
    LOG_INFO("Capture", "Started at %dx%d", static_cast<int>(_displayWidth), static_cast<int>(_displayHeight));
}

void Application::StepCaptureFrame(double deltaSeconds) {
    if (!_capture.active) {
        return;
    }
    _capture.stepSeconds = std::max(0.0, deltaSeconds);
#ifdef __EMSCRIPTEN__
    if (_capture.resized) {
        // Nothing should resize the canvas mid-capture, but a page layout change or a browser
        // callback that slipped in would cost the video a wrong frame: put the size back.
        int canvasWidth = 0, canvasHeight = 0;
        emscripten_get_canvas_element_size("#canvas", &canvasWidth, &canvasHeight);
        if (canvasWidth != _capture.width || canvasHeight != _capture.height) {
            emscripten_set_canvas_element_size("#canvas", _capture.width, _capture.height);
        }
        if (canvasWidth != _capture.width || canvasHeight != _capture.height
            || _displayWidth != _capture.width || _displayHeight != _capture.height) {
            LOG_WARN("Capture", "Render size drifted to %dx%d; restoring %dx%d", canvasWidth, canvasHeight,
                     _capture.width, _capture.height);
            HandleResize(_capture.width, _capture.height);
        }
    }
#endif
    RunOneFrame();
}

void Application::EndCapture() {
    if (!_capture.active) {
        return;
    }
    _capture.active = false;
    // The next live frame measures from now, not from before the capture began.
    _lastFrame = glfwGetTime();
#ifdef __EMSCRIPTEN__
    if (_capture.resized) {
        _capture.resized = false;
        FitCanvasToCss();
    }
#endif
    LOG_INFO("Capture", "Ended");
}

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
            if (component.planet && catalogIndex < OrbitLayout::kBodyCount) {
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
    std::string json = R"([{"index":0,"id":"sun","name":"Sun","parent":null,"kind":"star","radiusKm":)" +
                       std::to_string(SceneScale::kSunRadiusKm) + "}";
    char buffer[256];
    for (const BodyCatalog::Entry& entry : BodyCatalog::kEntries) {
        const std::string parent = entry.parentId ? std::string("\"") + entry.parentId + "\"" : "null";
        std::snprintf(buffer, sizeof(buffer), R"(,{"index":%d,"id":"%s","name":"%s","parent":%s,"kind":"%s","radiusKm":%.3f})",
                      entry.index, entry.id, AsciiName(entry.displayNameEn).c_str(), parent.c_str(),
                      KindSlug(entry.kind), static_cast<double>(entry.radiusKm));
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
                   entry && entry->kind != BodyCatalog::Kind::Satellite && index < OrbitLayout::kBodyCount) {
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
    for (size_t i = 0; i < _missionCatalog.missions.size(); ++i) {
        if (i > 0) {
            json += ',';
        }
        glm::dvec3 probe(0.0);
        if (!SampleMissionHeliocentricPosition(static_cast<int>(i), probe)) {
            json += "null";
            continue;
        }
        std::snprintf(buffer, sizeof(buffer), "[%.17g,%.17g,%.17g]", probe.x, probe.y, probe.z);
        json += buffer;
    }
    json += "]}";
    return json;
}

void Application::PreloadBodySystem(int catalogIndex) {
    const BodyCatalog::Entry* entry = BodyCatalog::FindByIndex(catalogIndex);
    if (entry && entry->kind == BodyCatalog::Kind::Satellite && entry->parentId) {
        entry = BodyCatalog::FindById(entry->parentId);
    }
    if (entry && entry->index > 0 && entry->index < OrbitLayout::kBodyCount) {
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
    return (toggles.hints ? 1 : 0) | (toggles.planetStarDistances ? 2 : 0) | (toggles.satelliteDistances ? 4 : 0)
        | (toggles.stagingLabels ? 8 : 0);
}
