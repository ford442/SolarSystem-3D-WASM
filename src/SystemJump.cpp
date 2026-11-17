// Travel between star systems (docs/ARCHITECTURE.md §12): RequestSystemJump's fade, the scene
// swap in LoadSystem, and where the camera lands. The catalog side — which rows are active —
// is StarSystemCatalog; this file only rebuilds the scene from it.
#include "Application.h"
#include "JsBridge.h"
#include "SimState.h"
#include "assets/TextureLoadingQueue.h"
#include "core/Log.h"
#include "Solar_System/BodyCatalog.h"
#include "Solar_System/OrbitLayout.h"
#include "Solar_System/SceneScale.h"
#include "Solar_System/StarSystemCatalog.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <ratio>
#include <string>
#include <utility>

namespace {

// The jump's three phases, in seconds. Out: the frame washes into the departing star's light
// and on to black. Black: the swap happens on its first frame (it can take a few frames' time
// on web while the new planetLighting variants compile), the rest hides the first frames of
// the new scene. In: the arrival fades up while the camera flies in.
constexpr double kJumpOutSeconds = 0.9;
constexpr double kJumpBlackSeconds = 0.25;
constexpr double kJumpInSeconds = 1.2;

// The clock on arrival. Sol keeps SimState's default (1 = a year per two minutes). Another
// system's planets orbit in days — TRAPPIST-1 b in 1.5 — so at that rate they would circle
// their star twice a second, and the landing view would swing to the night side at once (the
// camera follows its planet's motion, not its orbit's turn). Instead the landing planet moves
// about a degree of its orbit per second: 240x its period in days times real time, held to
// 100–10 000x (10 000x shows the b:c:d resonance), and never below the settings slider's
// 0.01 floor (settingsPanel.ts MIN_TIME_SCALE; ~2 600x real time, TRAPPIST-1 e's ~1.7°/s).
constexpr double kArrivalDegreesPerSecond = 1.0;
constexpr double kMinRealTimeMultiple = 100.0;
constexpr double kMaxRealTimeMultiple = 10000.0;
constexpr float kMinUiTimeScale = 0.01f;

float ArrivalTimeScale(int landingIndex) {
    if (StarSystemCatalog::IsSol()) {
        return SimState{}.timeScale;
    }
    double periodDays = 0.0;
    if (landingIndex > 0 && landingIndex < OrbitLayout::BodyCount()) {
        periodDays = OrbitLayout::GetOrbitalPeriodDays(static_cast<OrbitLayout::Body>(landingIndex));
    }
    const double multiple = periodDays > 0.0 ? std::clamp(86400.0 * periodDays / 360.0 * kArrivalDegreesPerSecond,
                                                          kMinRealTimeMultiple, kMaxRealTimeMultiple)
                                             : kMaxRealTimeMultiple;
    const double daysPerSimSecondAt1x =
        static_cast<double>(OrbitLayout::kEarthYearDays) / static_cast<double>(OrbitLayout::kEarthOrbitSecondsAt1x);
    return std::max(kMinUiTimeScale, static_cast<float>(multiple / 86400.0 / daysPerSimSecondAt1x));
}

int LandingIndex(int focusIndex) {
    return focusIndex >= 0 && focusIndex < OrbitLayout::BodyCount() ? focusIndex
                                                                    : StarSystemCatalog::EarthLikeFocusIndex();
}

#ifdef __EMSCRIPTEN__
// EM_JS bodies are JavaScript, and clang-format would split `===`.
// clang-format off
// `?<name>=` on the page, "" when absent (same shape as JobSystem's `?jobs=` helper). The
// caller frees the returned string.
EM_JS(char*, PageQueryParameterJs, (const char* name), {
    try {
        if (typeof location === 'undefined') {
            return 0;
        }
        const value = new URLSearchParams(location.search).get(UTF8ToString(name));
        if (!value || value.length > 64) {
            return 0;
        }
        const length = lengthBytesUTF8(value) + 1;
        const buffer = _malloc(length);
        stringToUTF8(value, buffer, length);
        return buffer;
    } catch (e) {
        return 0;
    }
});
// clang-format on

std::string PageQueryParameter(const char* name) {
    char* raw = PageQueryParameterJs(name);
    if (!raw) {
        return {};
    }
    std::string value(raw);
    std::free(raw);
    return value;
}
#endif

float Smoothstep(float edge0, float edge1, float x) {
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

} // namespace

const std::string& Application::GetActiveSystemId() const {
    return StarSystemCatalog::ActiveId();
}

bool Application::RequestSystemJump(const std::string& id, int focusIndex) {
    if (!StarSystemCatalog::IsValidId(id)) {
        NotifySystemChanged(id.c_str(), -1);
        return false;
    }
    if (!_sun || _appState != AppState::RUNNING) {
        // Before the scene exists (a ?system= deep link while the core resources download):
        // select the system; InitSceneObjects builds it and lands the camera.
        const bool ok = LoadSystem(id, focusIndex);
        NotifySystemChanged(id.c_str(), ok ? 1 : -1);
        return ok;
    }
    if (id == StarSystemCatalog::ActiveId()) {
        LandCameraInSystem(focusIndex);
        return true;
    }
    // Check the catalog exists before fading out, so a bad id does not blank the screen.
    if (id != StarSystemCatalog::kSolId) {
        std::string error;
        if (!StarSystemCatalog::LoadRuntimeJson(StarSystemCatalog::RuntimeJsonPath(id), error)) {
            LOG_ERROR("StarSystem", "Cannot jump to \"%s\": %s", id.c_str(), error.c_str());
            NotifySystemChanged(id.c_str(), -1);
            return false;
        }
    }
    _jump.phase = SystemJump::Phase::Out;
    _jump.seconds = 0.0;
    _jump.target = id;
    _jump.focusIndex = focusIndex;
    _jump.departureColor = _sun->GetLightTint() * glm::vec3(1.0f, 0.93f, 0.82f);
    LOG_INFO("StarSystem", "Jumping %s → %s", StarSystemCatalog::ActiveId().c_str(), id.c_str());
    NotifySystemChanged(id.c_str(), 0);
    return true;
}

void Application::UpdateSystemJump(double deltaSeconds) {
    if (_jump.phase == SystemJump::Phase::None) {
        _screenFadeAmount = 0.0f;
        return;
    }
    _jump.seconds += deltaSeconds;
    switch (_jump.phase) {
        case SystemJump::Phase::Out: {
            const auto f = static_cast<float>(_jump.seconds / kJumpOutSeconds);
            _screenFadeAmount = Smoothstep(0.0f, 0.6f, f);
            _screenFadeColor = _jump.departureColor * (1.0f - Smoothstep(0.45f, 1.0f, f));
            if (_jump.seconds >= kJumpOutSeconds) {
                _jump.phase = SystemJump::Phase::Black;
                _jump.seconds = 0.0;
                _screenFadeAmount = 1.0f;
                _screenFadeColor = glm::vec3(0.0f);
                const bool ok = LoadSystem(_jump.target, _jump.focusIndex);
                NotifySystemChanged(_jump.target.c_str(), ok ? 1 : -1);
            }
            break;
        }
        case SystemJump::Phase::Black:
            _screenFadeAmount = 1.0f;
            _screenFadeColor = glm::vec3(0.0f);
            if (_jump.seconds >= kJumpBlackSeconds) {
                _jump.phase = SystemJump::Phase::In;
                _jump.seconds = 0.0;
            }
            break;
        case SystemJump::Phase::In: {
            const auto f = static_cast<float>(_jump.seconds / kJumpInSeconds);
            _screenFadeAmount = 1.0f - Smoothstep(0.0f, 1.0f, f);
            _screenFadeColor = glm::vec3(0.0f);
            if (_jump.seconds >= kJumpInSeconds) {
                _jump.phase = SystemJump::Phase::None;
                _screenFadeAmount = 0.0f;
                NotifySystemChanged(_jump.target.c_str(), 2);
            }
            break;
        }
        case SystemJump::Phase::None: break;
    }
}

bool Application::LoadSystem(const std::string& id, int focusIndex) {
    if (!StarSystemCatalog::IsValidId(id)) {
        return false;
    }
    std::unique_ptr<StarSystemCatalog::System> system;
    if (id != StarSystemCatalog::kSolId) {
        std::string error;
        system = StarSystemCatalog::LoadRuntimeJson(StarSystemCatalog::RuntimeJsonPath(id), error);
        if (!system) {
            LOG_ERROR("StarSystem", "Cannot load \"%s\": %s", id.c_str(), error.c_str());
            return false;
        }
    }

    if (!_sun) {
        // No scene yet: only the catalog changes. InitSceneObjects builds this system.
        if (system) {
            StarSystemCatalog::Activate(std::move(system));
        } else {
            StarSystemCatalog::ActivateSol();
        }
        _pendingSystemLanding = true;
        _pendingSystemFocus = focusIndex;
        _simState.timeScale = ArrivalTimeScale(LandingIndex(focusIndex));
        return true;
    }
    if (id == StarSystemCatalog::ActiveId()) {
        LandCameraInSystem(focusIndex);
        return true;
    }

    const auto started = std::chrono::steady_clock::now();
    SetObserveMode(false);
    StopMissionFollow();
    TearDownStarSystem();
    if (system) {
        StarSystemCatalog::Activate(std::move(system));
    } else {
        StarSystemCatalog::ActivateSol();
    }
    _renderer.PrefetchCatalogVariants();
    InitStarSystem();
    // The sky-event caches are Sol's; searched afresh after a round trip.
    _nextConjunctionCached = false;
    _nextSkyEventCached = false;
    PlaceBodies();
    _simState.timeScale = ArrivalTimeScale(LandingIndex(focusIndex));
    NotifySettingsChanged("timeScale");
    LandCameraInSystem(focusIndex);
    LOG_INFO("StarSystem", "Now in %s: %d bodies around %s (%.0f ms)", id.c_str(), OrbitLayout::BodyCount(),
             BodyCatalog::Star().id,
             std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
    return true;
}

void Application::TearDownStarSystem() {
    // Downloads and decodes in flight point at textures about to be destroyed.
    TextureLoadingQueue::GetInstance().CancelAll();
    ++_manifestGeneration;
    _planetSystemManifests.clear();

    SetCameraAnchor(nullptr, false);
    _drawOrder.clear();
    _frameScene = SceneSnapshot{};
    _renderableSceneComponents.clear();
    _sun.reset();
    _nearestPlanetIndex = -1;
    _focusedPlanetIndex = -1;
}

void Application::LandCameraInSystem(int focusIndex) {
    const int index = LandingIndex(focusIndex);
    const auto body = static_cast<OrbitLayout::Body>(index);

    // Start well outside the target, on its day side, then let FocusPlanetByIndex fly in to a
    // gibbous view of it: the arrival reads as approaching a lit planet rather than appearing
    // next to a dark one.
    glm::dvec3 target(0.0);
    double sizeFactor = SceneScale::SunSizeFactor();
    if (index > 0) {
        target = OrbitLayout::GetHeliocentricScenePosition(body);
        if (const BodyCatalog::Entry* entry = BodyCatalog::FindByIndex(index)) {
            sizeFactor = SceneScale::BodySizeFactor(*entry);
        }
    }
    const glm::dvec3 outward = glm::length(target) > 0.0 ? glm::normalize(target) : glm::dvec3(1.0, 0.0, 0.0);
    glm::dvec3 side = glm::cross(outward, glm::dvec3(0.0, 1.0, 0.0));
    side = glm::length(side) > 1e-6 ? glm::normalize(side) : glm::dvec3(0.0, 0.0, 1.0);
    // Sunward (the day side faces -outward), off to one side and a little above the plane.
    const glm::dvec3 direction = index > 0 ? glm::normalize(-outward * 0.75 + side * 0.55 + glm::dvec3(0.0, 0.3, 0.0))
                                           : glm::normalize(glm::dvec3(0.7, 0.3, 0.7));
    const double distance = std::max(0.3 * glm::length(target), 150.0 * sizeFactor);
    const glm::dvec3 start = target + direction * distance;
    const glm::dvec3 look = -direction;
    SetCameraHeliocentricPose(start, static_cast<float>(glm::degrees(std::atan2(look.z, look.x))),
                              static_cast<float>(glm::degrees(std::asin(glm::clamp(look.y, -1.0, 1.0)))));
    FocusPlanetByIndex(index, glm::vec3(direction));
}

void Application::ApplySystemEnvOverride() {
    // Which system the scene starts in. Web: the page's `?system=<id>&planet=<name>` (read here,
    // before the scene is built, so a deep link opens in the system with no jump from Sol).
    // Native: SOLARSYSTEM_SYSTEM=<id>[:<planet>], e.g. trappist-1 or trappist-1:e. The planet
    // is anything FocusIndexForName accepts; without one the camera lands on the Earth-like
    // planet. Sol's own `planet=` is the page's (deepLink.ts), so Sol is left alone here.
    std::string id;
    std::string planet;
#ifdef __EMSCRIPTEN__
    id = PageQueryParameter("system");
    planet = PageQueryParameter("planet");
#else
    if (const char* spec = std::getenv("SOLARSYSTEM_SYSTEM")) {
        id = spec;
        if (const std::size_t colon = id.find(':'); colon != std::string::npos) {
            planet = id.substr(colon + 1);
            id.resize(colon);
        }
    }
#endif
    for (char& c : id) {
        c = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    }
    if (id.empty() || id == StarSystemCatalog::kSolId) {
        return;
    }
    if (!LoadSystem(id, -1)) {
        LOG_WARN("StarSystem", "system \"%s\": no such system; staying in Sol", id.c_str());
        return;
    }
    if (planet.empty()) {
        return;
    }
    const int focus = StarSystemCatalog::FocusIndexForName(planet);
    if (focus < 0) {
        LOG_WARN("StarSystem", "planet \"%s\" is not in %s; landing on the Earth-like planet", planet.c_str(),
                 id.c_str());
        return;
    }
    _pendingSystemFocus = focus;
    _simState.timeScale = ArrivalTimeScale(focus);
}
