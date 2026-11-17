// Space weather, Application's side (docs/ARCHITECTURE.md §13): the model in astro/SpaceWeather
// evaluated at this frame's date and mapped into the scene (SpaceWeatherFrame), plus the
// settings-panel API. Renderer draws what lands in _frameScene.spaceWeather; nothing here
// touches GL.
#include "Application.h"
#include "ApplicationTypes.h"
#include "FrameContext.h"
#include "JsBridge.h"
#include "QualitySettings.h"
#include "Solar_System/BodyCatalog.h"
#include "Solar_System/OrbitLayout.h"
#include "Solar_System/Planet.h"
#include "Solar_System/StarSystemCatalog.h"
#include "astro/MagneticFieldModel.h"
#include "astro/MagneticFieldParams.h"
#include "astro/SpaceWeather.h"
#include "core/Log.h"
#include "gfx/Frustum.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>
#include <glm/glm.hpp>

namespace {
constexpr double kDaysPerOrrerySecond =
    static_cast<double>(OrbitLayout::kEarthYearDays) / static_cast<double>(OrbitLayout::kEarthOrbitSecondsAt1x);
constexpr double kSecondsPerDay = 86400.0;
// The background stream's reach: past Jupiter's orbit, so it is culled from Saturn outward.
constexpr float kWindEdgeAu = 6.5f;
// What the particles may advance per visual second at 400 km/s: physical at slow rates, held
// readable at fast ones (at 1× the orrery would carry the wind 0.7 AU a second).
constexpr double kMinAuPerSecond = 0.02;
constexpr double kMaxAuPerSecond = 0.25;
// A magnetosphere group draws once its quiet standoff spans this many pixels.
constexpr float kMinStandoffPixels = 6.0f;
// Compressed scale: the planets sit far closer than their magnetospheres reach (Jupiter's
// would swallow the inner system), so the drawn standoff is capped against the body's
// distance from the Sun. Shapes and the auroral ovals keep their physical values.
constexpr double kMaxTailFractionOfOrbit = 0.35;
constexpr double kTailRadiiPerStandoff = 20.0; // solarWindUpdate.vs respawns a parcel past -20 s
constexpr std::size_t kMaxKnots = 16;          // MagnetosphereUBO's knotAu / knotScene

/** Whether any view frustum sees a sphere, and how many pixels its radius spans in view 0. */
struct Visibility {
    bool seen = false;
    float pixels = 0.0f;
};

Visibility SphereVisibility(const std::array<CameraView, 2>& views, int viewCount, const glm::vec3& center,
                            float radius) {
    Visibility visibility;
    for (int view = 0; view < viewCount; ++view) {
        const CameraView& v = views[static_cast<std::size_t>(view)];
        const Frustum frustum(v.projection * v.view);
        if (frustum.Intersects(BoundingSphere{center, radius})) {
            visibility.seen = true;
        }
    }
    const CameraView& v0 = views[0];
    // The render origin is on the camera, so the camera is (near) the origin of the render frame.
    const float distance = std::max(glm::length(center - v0.headPosition), 1e-6f);
    const float focal = v0.projection[1][1] * 0.5f * static_cast<float>(std::max(v0.viewport.w, 1));
    visibility.pixels = radius / distance * focal;
    return visibility;
}

glm::vec3 ToVec3(const BodyCatalog::Rgb& color) {
    return glm::vec3(color.r, color.g, color.b);
}

} // namespace

void Application::InitSpaceWeather() {
    _spaceWeather.ClearLaunched();
    _spaceWeather.SetCycle(SpaceWeather::CycleFromStar(BodyCatalog::Star()));
    if (!_spaceWeatherLoaded) {
        _spaceWeatherLoaded = true;
        SpaceWeather::CmeCatalog catalog;
        std::string error;
        if (SpaceWeather::LoadCmeCatalog("resource/spaceweather/cmes.json", catalog, error)) {
            LOG_INFO("SpaceWeather", "%zu CMEs in the catalog", catalog.events.size());
            _spaceWeather.SetCatalog(std::move(catalog));
        } else {
            LOG_WARN("SpaceWeather", "No CME catalog (%s); storms come only from Launch CME", error.c_str());
        }
#ifndef __EMSCRIPTEN__
        // Native counterparts of the web deep link's &kp= / &cme= (and the wind toggle).
        if (const char* wind = std::getenv("SOLARSYSTEM_SOLAR_WIND")) {
            _spaceWeatherWind = std::strcmp(wind, "0") != 0;
        }
        if (const char* kp = std::getenv("SOLARSYSTEM_KP")) {
            _spaceWeather.SetKpOverride(std::atof(kp));
        }
        if (const char* cme = std::getenv("SOLARSYSTEM_CME")) {
            JumpToSpaceWeatherDate(cme);
        }
#endif
    }
}

void Application::UpdateSpaceWeather(int viewCount) {
    SpaceWeatherFrame& frame = _spaceWeatherFrame;
    _frameScene.spaceWeather = nullptr;
    if (!_sun) {
        return;
    }
    const QualityTierSettings quality = GetQualitySettings(_simState.qualityPreset, _simState.isMobileWeb);
    const double jd = OrbitLayout::GetJulianDate();
    const bool sol = StarSystemCatalog::IsSol();

    // The cycle runs for any star that has one (the photosphere's spots); the wind, the
    // magnetospheres and the CMEs are Sol's.
    std::vector<glm::dvec3> positions;
    const auto& magnetospheres = SpaceWeather::Magnetospheres();
    positions.reserve(magnetospheres.size());
    for (const SpaceWeather::Magnetosphere& m : magnetospheres) {
        glm::dvec3 lonLatR(0.0, 0.0, 1.0);
        if (sol) {
            const glm::dvec3 p = OrbitLayout::GetHeliocentricScenePosition(m.body);
            const double r = glm::length(p);
            if (r > 0.0) {
                lonLatR = glm::dvec3(std::atan2(p.z, p.x), std::asin(glm::clamp(p.y / r, -1.0, 1.0)),
                                     static_cast<double>(OrbitLayout::GetHeliocentricRadiusAu(m.body)));
            }
        }
        positions.push_back(lonLatR);
    }
    _spaceWeather.Evaluate(jd, positions);

    frame.auroraMode = quality.auroraMode;
    frame.photosphereMode = quality.photosphereMode;
    frame.windEnabled = sol && _spaceWeatherWind && quality.solarWindParticles > 0;
    frame.particles = frame.windEnabled ? quality.solarWindParticles : 0;
    const SpaceWeather::CycleState& cycle = _spaceWeather.Cycle();
    frame.cyclePhase = static_cast<float>(cycle.phase);
    frame.beltHalfWidthRad = static_cast<float>(glm::radians(SpaceWeather::StreamerBeltHalfWidthDeg(cycle.phase)));
    frame.spotBandsRad = glm::vec4(glm::radians(cycle.current.latitudeDeg), glm::radians(cycle.current.halfWidthDeg),
                                   glm::radians(cycle.previous.latitudeDeg), glm::radians(cycle.previous.halfWidthDeg));
    frame.spotCoverage = glm::vec2(cycle.current.coverage, cycle.previous.coverage);
    frame.spotDays = static_cast<float>(cycle.yearsSinceMinimum * 365.25);

    // The particles' clock: the frame delta (a fixed step in a capture), stopped while paused.
    const bool paused = _simState.timePaused || _observe.active;
    frame.visualDt = paused ? 0.0f : static_cast<float>(std::min(_deltaTime, 0.1));
    _spaceWeatherClock += frame.visualDt;
    frame.visualClock = static_cast<float>(std::fmod(_spaceWeatherClock, 86400.0));
    ++frame.frameCounter;
    const double daysPerSecond = static_cast<double>(_simState.timeScale) * kDaysPerOrrerySecond;
    const double auPerSecond = SpaceWeather::kSlowWindKmS * daysPerSecond * kSecondsPerDay / SpaceWeather::kAuKm;
    frame.auPerSecond = static_cast<float>(glm::clamp(auPerSecond, kMinAuPerSecond, kMaxAuPerSecond));
    // Corotation such that a 400 km/s parcel trails the Parker angle at 1 AU, whatever the rate.
    frame.sunRotation =
        frame.auPerSecond *
        static_cast<float>(std::tan(SpaceWeather::ParkerSpiralAngleRad(1.0, SpaceWeather::kSlowWindKmS)));
    frame.edgeAu = kWindEdgeAu;

    frame.knots.clear();
    for (const glm::dvec2& knot : OrbitLayout::AuToSceneKnots()) {
        if (frame.knots.size() < kMaxKnots) {
            frame.knots.emplace_back(static_cast<float>(knot.x), static_cast<float>(knot.y));
        }
    }
    const float sceneUnitsPerAuNearSun =
        frame.knots.empty() ? OrbitLayout::kAuToSceneUnits : frame.knots.front().y / frame.knots.front().x;
    frame.spawnAu = 1.3f * _sun->GetSceneRadius() / sceneUnitsPerAuNearSun;

    // Background group: the inner heliosphere, while it is on screen and the camera is not far
    // out past it (at Pluto it would be a few pixels of haze).
    const glm::vec3 sunRender = _sun->GetPosition();
    const float edgeScene = static_cast<float>(OrbitLayout::AuToSceneDistanceDouble(kWindEdgeAu));
    const Visibility heliosphere = SphereVisibility(_frameViews, viewCount, sunRender, edgeScene);
    const float cameraFromSun = glm::length(sunRender - _frameViews[0].headPosition);
    frame.backgroundActive =
        frame.windEnabled && heliosphere.seen && heliosphere.pixels > 40.0f && cameraFromSun < 3.0f * edgeScene;

    frame.bodies.clear();
    if (sol) {
        const auto& states = _spaceWeather.Bodies();
        for (std::size_t i = 0; i < magnetospheres.size() && i < states.size(); ++i) {
            const SpaceWeather::Magnetosphere& m = magnetospheres[i];
            const SpaceWeather::BodyState& state = states[i];
            const RenderableSceneComponent* component = nullptr;
            for (const auto& candidate : _renderableSceneComponents) {
                const BodyCatalog::Entry* entry = candidate.planet ? candidate.planet->CatalogEntry() : nullptr;
                if (entry && std::strcmp(entry->id, m.id) == 0) {
                    component = &candidate;
                    break;
                }
            }
            if (!component) {
                continue; // its system has not loaded
            }
            const Planet& planet = *component->planet;
            SpaceWeatherFrame::Body body;
            body.slot = static_cast<int>(i);
            body.body = static_cast<int>(m.body);
            body.heliocentric = planet.GetWorldPosition();
            body.render = planet.GetPosition();
            body.radius = std::max(planet.GetRadius(), 1e-6f);
            const MagneticFieldParams params = MagneticFieldCatalog::IntrinsicParamsForBody(m.body);
            body.pole = glm::normalize(glm::mat3(planet.GetRotationMatrix()) *
                                       (MagneticFieldModel::DipoleFrame(params) * glm::vec3(0.0f, 1.0f, 0.0f)));
            const double helioDistance = glm::length(body.heliocentric);
            body.sunward = helioDistance > 0.0 ? glm::vec3(-body.heliocentric / helioDistance) : glm::vec3(1, 0, 0);

            double drawScale = 1.0;
            if (OrbitLayout::GetScaleMode() != OrbitLayout::ScaleMode::Realistic && state.quietStandoffRadii > 0.0) {
                const double cap = kMaxTailFractionOfOrbit * helioDistance /
                                   (kTailRadiiPerStandoff * static_cast<double>(body.radius));
                drawScale = std::min(1.0, cap / state.quietStandoffRadii);
            }
            body.standoff = static_cast<float>(std::max(state.standoffRadii * drawScale, 1.2));
            body.quietStandoff = static_cast<float>(std::max(state.quietStandoffRadii * drawScale, 1.2));
            body.flaring = static_cast<float>(state.flaringExponent);
            body.kp = static_cast<float>(state.kp);
            body.windRatio = static_cast<float>(state.wind.speedKmS / SpaceWeather::kSlowWindKmS);
            body.cmeStrength = static_cast<float>(state.wind.cmeStrength);
            const double quietDensity = SpaceWeather::kQuietDensityAt1AuPerCm3 / std::max(state.rAu * state.rAu, 1e-4);
            body.densityRatio = static_cast<float>(state.wind.densityPerCm3 / quietDensity);
            body.ovalRad =
                glm::vec4(glm::radians(state.oval.equatorwardMidnightDeg), glm::radians(state.oval.equatorwardNoonDeg),
                          glm::radians(state.oval.polewardMidnightDeg), glm::radians(state.oval.polewardNoonDeg));
            if (const BodyCatalog::AuroraRow* row = BodyCatalog::FindAurora(m.id)) {
                body.colorLow = ToVec3(row->colorLow);
                body.colorHigh = ToVec3(row->colorHigh);
                body.colorEdge = ToVec3(row->colorEdge);
                body.brightness = row->brightness;
                body.shellBottom = 1.0f + row->bottomKm / std::max(planet.CatalogEntry()->radiusKm, 1.0f);
                body.shellTop = 1.0f + row->topKm / std::max(planet.CatalogEntry()->radiusKm, 1.0f);
            }

            if (frame.windEnabled) {
                // The group spans [+6 s, -20 s] along the Sun line and 7 s across it.
                const float extent = 21.0f * body.quietStandoff * body.radius;
                const glm::vec3 center = body.render - body.sunward * (7.0f * body.quietStandoff * body.radius);
                const Visibility seen = SphereVisibility(_frameViews, viewCount, center, extent);
                const Visibility standoff =
                    SphereVisibility(_frameViews, viewCount, body.render, body.quietStandoff * body.radius);
                body.groupActive = seen.seen && standoff.pixels > kMinStandoffPixels;
            }
            frame.bodies.push_back(body);
        }

        frame.cmes.clear();
        for (const SpaceWeather::Cme* cme : _spaceWeather.ActiveCmes()) {
            SpaceWeatherFrame::Cme c;
            c.direction = glm::vec3(cme->direction);
            c.frontAu = static_cast<float>(SpaceWeather::FrontKm(*cme, jd) / SpaceWeather::kAuKm);
            c.cosEdge = static_cast<float>(std::cos(cme->halfAngleRad * SpaceWeather::kFlankReach));
            c.cosCore = static_cast<float>(std::cos(cme->halfAngleRad * 0.6));
            const double speed = SpaceWeather::FrontSpeedKmS(*cme, jd);
            c.speedRatio = static_cast<float>(speed / SpaceWeather::kSlowWindKmS);
            c.brightness = static_cast<float>(glm::clamp(0.5 + speed / 2000.0, 0.6, 1.6));
            frame.cmes.push_back(c);
        }
    } else {
        frame.cmes.clear();
    }
    _frameScene.spaceWeather = &frame;
}

void Application::SetSolarWindEnabled(bool enabled) {
    if (_spaceWeatherWind == enabled) {
        return;
    }
    _spaceWeatherWind = enabled;
    LOG_INFO("SpaceWeather", "Solar wind %s", enabled ? "on" : "off");
    NotifySettingsChanged("solarWind");
}

void Application::SetKpOverride(double kp) {
    _spaceWeather.SetKpOverride(kp);
    NotifySettingsChanged("kpOverride");
}

void Application::LaunchCmeNow() {
    if (!StarSystemCatalog::IsSol()) {
        return;
    }
    const SpaceWeather::Cme& cme = _spaceWeather.LaunchCme(OrbitLayout::GetJulianDate());
    LOG_INFO("SpaceWeather", "%s: %.0f km/s, at Earth in %.1f days", cme.name.c_str(), cme.speedKmS,
             cme.modelArrivalJd > 0.0 ? cme.modelArrivalJd - cme.launchJd : 0.0);
    SetSolarWindEnabled(true);
}

bool Application::PlaySpaceWeatherEvent(int index) {
    const auto& events = _spaceWeather.Catalog().events;
    if (!StarSystemCatalog::IsSol() || index < 0 || index >= static_cast<int>(events.size())) {
        return false;
    }
    const SpaceWeather::Cme& cme = events[static_cast<std::size_t>(index)];
    // Start a little before launch so the pulse is seen leaving the Sun.
    OrbitLayout::SetJulianDate(cme.launchJd - 0.05);
    const double arrival = cme.ArrivalAtEarthJd();
    const double transitDays = arrival > 0.0 ? arrival - cme.launchJd : 2.0;
    constexpr double kPlaySeconds = 8.0;
    _simState.timeScale =
        static_cast<float>(glm::clamp(transitDays / (kPlaySeconds * kDaysPerOrrerySecond), 0.01, 1.0));
    _simState.timePaused = false;
    LOG_INFO("SpaceWeather", "Playing %s (launch JD %.3f), time scale %.3f", cme.name.c_str(), cme.launchJd,
             static_cast<double>(_simState.timeScale));
    NotifySettingsChanged("timeScale");
    NotifySettingsChanged("paused");
    NotifySettingsChanged("simulationEpoch");
    SetSolarWindEnabled(true);
    return true;
}

bool Application::JumpToSpaceWeatherDate(const std::string& isoUtc) {
    const double jd = SpaceWeather::ParseIsoUtc(isoUtc);
    if (jd <= 0.0) {
        LOG_WARN("SpaceWeather", "Not an ISO-8601 UTC time: %s", isoUtc.c_str());
        return false;
    }
    OrbitLayout::SetJulianDate(jd);
    NotifySettingsChanged("simulationEpoch");
    SetSolarWindEnabled(true);
    return true;
}

std::string Application::GetSpaceWeatherJson() const {
    const QualityTierSettings quality = GetQualitySettings(_simState.qualityPreset, _simState.isMobileWeb);
    char extra[256];
    std::snprintf(extra, sizeof(extra),
                  "\"sol\":%s,\"solarWind\":%s,\"particles\":%d,\"auroraMode\":%d,\"photosphereMode\":%d",
                  StarSystemCatalog::IsSol() ? "true" : "false", _spaceWeatherWind ? "true" : "false",
                  _spaceWeatherFrame.particles, quality.auroraMode, quality.photosphereMode);
    return _spaceWeather.ToJson(extra);
}
