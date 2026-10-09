// Observe mode: the camera stands on Earth at a lat/lon and the sky is drawn by ObserveSky.
// State machine and JS-facing state only — the maths is Observer.cpp, the drawing is
// ObserveSky.cpp / Renderer::RenderObserveSky. See docs/ARCHITECTURE.md.
#include "Application.h"
#include "JsBridge.h"
#include "Auxiliary_Modules/Ephemeris.h"
#include "Solar_System/CatalogSatellite.h"
#include "Solar_System/OrbitLayout.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sstream>

namespace {

float wrapDegrees(float deg) {
    float w = std::fmod(deg, 360.0f);
    return w < 0.0f ? w + 360.0f : w;
}

} // namespace

void Application::SetObserveMode(bool active) {
    if (active == _observe.active) {
        return;
    }

    if (active) {
        _observe.savedPosition = _camera.GetPosition();
        _observe.savedYaw = _camera.GetYaw();
        _observe.savedPitch = _camera.GetPitch();
        _observe.savedZoom = _camera.GetZoom();
        _observe.savedZoomMin = _camera.GetZoomMin();
        _observe.savedZoomMax = _camera.GetZoomMax();
        _observe.savedMovementLocked = _camera.IsMovementLocked();

        StopMissionFollow();
        _camera.CancelTransition();
        _camera.SetMovementLocked(true);
        _camera.SetZoomRange(ObserveState::kMinFovDeg, ObserveState::kMaxFovDeg);
        _camera.SetZoom(ObserveState::kDefaultFovDeg);
        _observe.active = true;
        SetObserveView(ObserveState::kDefaultAzimuthDeg, ObserveState::kDefaultElevationDeg);
        _observeSkyValid = false;
    } else {
        _observe.active = false;
        _camera.SetMovementLocked(_observe.savedMovementLocked);
        _camera.SetZoomRange(_observe.savedZoomMin, _observe.savedZoomMax);
        _camera.SetZoom(_observe.savedZoom);
        _camera.CancelTransition();
        _camera.SetPosition(_observe.savedPosition);
        _camera.SetYawPitch(_observe.savedYaw, _observe.savedPitch);
    }
    NotifySettingsChanged("observeMode");
}

void Application::SetObserverSite(double latDeg, double lonDeg, double altM) {
    _observe.site.latDeg = std::clamp(latDeg, -90.0, 90.0);
    double lon = std::fmod(lonDeg + 180.0, 360.0);
    if (lon < 0.0) {
        lon += 360.0;
    }
    _observe.site.lonDeg = lon - 180.0;
    _observe.site.altM = std::clamp(altM, -500.0, 9000.0);
    _observeSkyValid = false;
    NotifySettingsChanged("observeSite");
}

void Application::SetObserveView(float azimuthDeg, float elevationDeg) {
    // Horizon frame: yaw -90 looks north (-Z), so azimuth = yaw + 90.
    _camera.SetYawPitch(wrapDegrees(azimuthDeg) - 90.0f, std::clamp(elevationDeg, -89.0f, 89.0f));
}

void Application::SetObserveFov(float fovDeg) {
    _camera.SetZoom(fovDeg);
}

void Application::SetObserveTimeRate(double rate) {
    _observe.timeRate = std::clamp(rate, 0.0, 1.0e7);
    NotifySettingsChanged("observeTimeRate");
}

const Observer::Sky& Application::CurrentObserveSky() const {
    const double jd = OrbitLayout::GetJulianDate();
    const Observer::Site& site = _observe.site;
    if (!_observeSkyValid || jd != _observeSkyJd || site.latDeg != _observeSkySite.latDeg ||
        site.lonDeg != _observeSkySite.lonDeg || site.altM != _observeSkySite.altM) {
        _observeSky = Observer::ComputeSky(jd, site);
        _observeSkyJd = jd;
        _observeSkySite = site;
        _observeSkyValid = true;
    }
    return _observeSky;
}

unsigned int Application::ObserveMoonTexture() const {
    for (const auto& component : _renderableSceneComponents) {
        for (const auto& satellite : component.satellites) {
            const auto* catalogSatellite = dynamic_cast<const CatalogSatellite*>(satellite.get());
            if (catalogSatellite && catalogSatellite->GetEntry().index == SkyEvents::kMoon) {
                return catalogSatellite->GetDiffuseTexture();
            }
        }
    }
    return 0;
}

const char* Application::ObserveEventVisibility() const {
    const SkyEvents::SkyEvent& event = GetNextSkyEvent();
    if (!event.valid) {
        return "none";
    }
    // Which bodies must be above the horizon for the event to be watchable from this site.
    int bodies[2] = {-1, -1};
    switch (event.kind) {
        case SkyEvents::EventKind::SolarEclipse:
        case SkyEvents::EventKind::Transit:
            bodies[0] = 0; // the Sun
            break;
        case SkyEvents::EventKind::LunarEclipse:
            bodies[0] = SkyEvents::kMoon;
            break;
        case SkyEvents::EventKind::ShadowTransit:
            bodies[0] = event.bodyB; // Jupiter
            break;
        case SkyEvents::EventKind::Conjunction:
            bodies[0] = event.bodyA;
            bodies[1] = event.bodyB;
            break;
        default:
            return "unknown";
    }
    bool all = true;
    for (const int body : bodies) {
        if (body < 0) {
            continue;
        }
        double alt = 0.0;
        if (!Observer::BodyAltitudeDeg(body, event.julianDate, _observe.site, alt)) {
            return "unknown";
        }
        all = all && alt > 0.0;
    }
    return all ? "above" : "below";
}

void Application::UpdateObserveFrame() {
    // The sky pass draws from the horizon frame, not from this position. Parking the camera
    // just above Earth keeps everything keyed on camera distance (staged planet loading, the
    // LOD manager, the nearest-planet search) behaving as if the viewer were there.
    const glm::vec3 earth = OrbitLayout::GetOffset(OrbitLayout::Body::Earth);
    _camera.SetPosition(earth + glm::vec3(0.0f, ObserveState::kEarthSurfaceSceneUnits, 0.0f));

    // Scene components are not drawn (so not re-placed) in Observe; keep Earth and its moons at
    // today's ephemeris position so the LOD manager and the Moon's texture follow the viewer.
    for (auto& component : _renderableSceneComponents) {
        if (!component.planet) {
            continue;
        }
        const std::wstring& wideName = component.planet->GetEngName();
        if (OrbitLayout::BodyFromName(std::string(wideName.begin(), wideName.end())) != OrbitLayout::Body::Earth) {
            continue;
        }
        component.planet->AdjustToParent(0.0f);
        for (const auto& satellite : component.satellites) {
            satellite->AdjustToParent(0.0f);
        }
    }
}

std::string Application::GetObserveStateJson() const {
    const Observer::Sky& sky = CurrentObserveSky();
    const Observer::SkyBody& sun = sky.bodies[Observer::kSkySun];
    const Observer::SkyBody& moon = sky.bodies[Observer::kSkyMoon];
    const double jd = OrbitLayout::GetJulianDate();
    const std::string utc = Observer::FormatUtc(jd);

    char buffer[832];
    std::snprintf(buffer, sizeof(buffer),
                  R"({"active":%s,"latDeg":%.6f,"lonDeg":%.6f,"altM":%.1f,"julianDate":%.6f,"utc":"%s",)"
                  R"("azDeg":%.2f,"elDeg":%.2f,"fovDeg":%.2f,"timeRate":%.4f,"paused":%s,)"
                  R"("lstDeg":%.4f,"sunAltDeg":%.3f,"sunAzDeg":%.3f,"moonAltDeg":%.3f,"moonAzDeg":%.3f,)"
                  R"("moonIllum":%.4f,"sunCoverage":%.4f,"moonLibLonDeg":%.3f,"moonLibLatDeg":%.3f,"eventVisibility":"%s","starCount":%d})",
                  _observe.active ? "true" : "false", _observe.site.latDeg, _observe.site.lonDeg,
                  _observe.site.altM, jd, utc.c_str(),
                  wrapDegrees(_camera.GetYaw() + 90.0f), _camera.GetPitch(), _camera.GetZoom(),
                  _observe.timeRate, gSimState->timePaused ? "true" : "false", sky.lstDeg,
                  sun.altDeg, sun.azDeg, moon.altDeg, moon.azDeg, moon.illuminatedFraction,
                  sky.sunCoverage, sky.moonSubEarthLonDeg, sky.moonSubEarthLatDeg, ObserveEventVisibility(),
                  _renderer.observeSky ? _renderer.observeSky->DrawnStarCount() : 0);
    return buffer;
}

void Application::ApplyObserveEnvOverride() {
    // lat,lon[,jd[,az,el,fov]] — lets a headless native run open straight into Observe.
    const char* spec = std::getenv("SOLARSYSTEM_OBSERVE");
    if (!spec || !*spec) {
        return;
    }
    double values[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    int parsed = 0;
    std::stringstream stream(spec);
    std::string token;
    while (parsed < 6 && std::getline(stream, token, ',')) {
        values[parsed++] = std::atof(token.c_str());
    }
    if (parsed < 2) {
        std::cerr << "[Observe] SOLARSYSTEM_OBSERVE needs lat,lon[,jd[,az,el,fov]]" << std::endl;
        return;
    }
    SetObserverSite(values[0], values[1], 0.0);
    if (parsed >= 3 && values[2] > 0.0) {
        OrbitLayout::SetJulianDate(values[2]);
        gSimState->timePaused = true;
    }
    SetObserveMode(true);
    if (parsed >= 5) {
        SetObserveView(static_cast<float>(values[3]), static_cast<float>(values[4]));
    }
    if (parsed >= 6 && values[5] > 0.0) {
        SetObserveFov(static_cast<float>(values[5]));
    }
    std::cout << "[Observe] lat " << values[0] << " lon " << values[1] << " at "
              << Observer::FormatUtc(OrbitLayout::GetJulianDate()) << " UTC" << std::endl;
}
