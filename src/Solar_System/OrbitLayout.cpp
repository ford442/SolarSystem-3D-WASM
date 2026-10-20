#include "OrbitLayout.h"

#include "../Auxiliary_Modules/Ephemeris.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

namespace OrbitLayout {
namespace {

struct BodyData {
    glm::vec3 compressedOffset;
    float auDistance;
    float orbitalPeriodDays;
    float inclinationDeg;
    float siderealRotationDays; // negative => retrograde
};


/** Art offset aligning the Earth diffuse map's prime meridian with GMST zero. */
constexpr float kEarthPrimeMeridianOffsetDeg = 0.0f;

// NASA fact-sheet averages for periods/inclination/sidereal day.
// Sidereal rotation days: Venus and Uranus are retrograde (negative).
// Generated from resource/planets.catalog.json — see OrbitLayoutBodies.generated.inc header
// comment. Run `node scripts/generate-planet-metadata.mjs` after editing the catalog.
constexpr BodyData kBodies[] = {
#include "OrbitLayoutBodies.generated.inc"
};

static_assert(sizeof(kBodies) / sizeof(kBodies[0]) == static_cast<std::size_t>(kBodyCount),
              "Generated body rows must cover every OrbitLayout::Body value — "
              "rerun scripts/generate-planet-metadata.mjs after editing the catalog.");

ScaleMode g_scaleMode = ScaleMode::Compressed;
double g_julianDate = Ephemeris::kJ2000;
// Kept in double: rounding these to float is what put a ~0.004-unit grid under Neptune.
double g_lonRad[kBodyCount] = {};
double g_latRad[kBodyCount] = {};
double g_rAu[kBodyCount] = {};
float g_axialSpinDeg[kBodyCount] = {};
glm::dvec3 g_renderOrigin(0.0);

int bodyIndex(Body body) {
    const int idx = static_cast<int>(body);
    return std::clamp(idx, 0, kBodyCount - 1);
}

void refreshEphemerisCache() {
    for (int i = 0; i < kBodyCount; ++i) {
        const Ephemeris::HelioLB pos = Ephemeris::Position(i, g_julianDate);
        g_lonRad[i] = pos.lonRad;
        g_latRad[i] = pos.latRad;
        g_rAu[i] = pos.rAu;
    }
}

/** GetOrbitRadius in double. */
double orbitRadius(Body body) {
    if (body == Body::Sun) {
        return 0.0;
    }
    if (UsesTrueRadius(body)) {
        return AuToSceneDistanceDouble(g_rAu[bodyIndex(body)]);
    }
    if (g_scaleMode == ScaleMode::Compressed) {
        return static_cast<double>(glm::length(GetCompressedOffset(body)));
    }
    return AuToSceneDistanceDouble(static_cast<double>(GetAuDistance(body)));
}

} // namespace

void SetScaleMode(ScaleMode mode) {
    g_scaleMode = mode;
}

ScaleMode GetScaleMode() {
    return g_scaleMode;
}

void SetJulianDate(double julianDate) {
    g_julianDate = julianDate;
    refreshEphemerisCache();
}

double GetJulianDate() {
    return g_julianDate;
}

void ResetForTests() {
    for (int i = 0; i < kBodyCount; ++i) {
        g_axialSpinDeg[i] = 0.0f;
    }
    g_scaleMode = ScaleMode::Compressed;
    g_renderOrigin = glm::dvec3(0.0);
    SetJulianDate(Ephemeris::kJ2000);
}

float GetOrbitPeriodSecondsAt1x(Body body) {
    const float days = GetOrbitalPeriodDays(body);
    if (days <= 0.0f) {
        return 0.0f;
    }
    return kEarthOrbitSecondsAt1x * (days / kEarthYearDays);
}

void Advance(float scaledDtSeconds) {
    if (scaledDtSeconds == 0.0f) {
        return;
    }

    const double simDays = static_cast<double>(scaledDtSeconds) *
                           (static_cast<double>(kEarthYearDays) / static_cast<double>(kEarthOrbitSecondsAt1x));
    g_julianDate += simDays;

    for (int i = 1; i < kBodyCount; ++i) {
        const float siderealDays = kBodies[i].siderealRotationDays;
        if (siderealDays != 0.0f) {
            g_axialSpinDeg[i] += 360.0f * (static_cast<float>(simDays) / siderealDays);
        }
    }

    refreshEphemerisCache();
}

glm::vec3 GetCompressedOffset(Body body) {
    return kBodies[bodyIndex(body)].compressedOffset;
}

float GetAuDistance(Body body) {
    return kBodies[bodyIndex(body)].auDistance;
}

double AuToSceneDistanceDouble(double au) {
    if (au <= 0.0) {
        return 0.0;
    }
    if (g_scaleMode == ScaleMode::Realistic) {
        return au * static_cast<double>(kAuToSceneUnits);
    }

    // Piecewise-linear remap through planet compressed orbit radii so the
    // main belt (and comets) sit between Mars and Jupiter in art scale.
    struct Key {
        double au;
        double scene;
    };
    const auto key = [](Body body) {
        return Key{GetAuDistance(body), glm::length(GetCompressedOffset(body))};
    };
    const Key keys[] = {
        key(Body::Mercury), key(Body::Venus), key(Body::Earth),
        key(Body::Mars), key(Body::Jupiter), key(Body::Saturn),
        key(Body::Uranus), key(Body::Neptune), key(Body::Pluto),
    };
    constexpr int keyCount = static_cast<int>(sizeof(keys) / sizeof(keys[0]));

    if (au <= keys[0].au) {
        return keys[0].scene * (au / keys[0].au);
    }
    for (int i = 0; i < keyCount - 1; ++i) {
        if (au <= keys[i + 1].au) {
            const double t = (au - keys[i].au) / (keys[i + 1].au - keys[i].au);
            return keys[i].scene + t * (keys[i + 1].scene - keys[i].scene);
        }
    }
    const Key& last = keys[keyCount - 1];
    return last.scene * (au / last.au);
}

float AuToSceneDistance(float au) {
    return static_cast<float>(AuToSceneDistanceDouble(static_cast<double>(au)));
}

glm::vec3 HelioAuToScene(const glm::vec3& auSceneAxes) {
    const float r = glm::length(auSceneAxes);
    if (r < 1.0e-8f) {
        return glm::vec3(0.0f);
    }
    return auSceneAxes * (AuToSceneDistance(r) / r);
}

glm::dvec3 HelioAuToSceneDouble(const glm::dvec3& auSceneAxes) {
    const double r = glm::length(auSceneAxes);
    if (r < 1.0e-12) {
        return glm::dvec3(0.0);
    }
    return auSceneAxes * (AuToSceneDistanceDouble(r) / r);
}

float GetOrbitalPeriodDays(Body body) {
    return kBodies[bodyIndex(body)].orbitalPeriodDays;
}

float GetSiderealRotationDays(Body body) {
    return kBodies[bodyIndex(body)].siderealRotationDays;
}

float GetOrbitRadius(Body body) {
    return static_cast<float>(orbitRadius(body));
}

bool UsesTrueRadius(Body body) {
    return static_cast<int>(body) >= static_cast<int>(Body::Eris);
}

float GetHeliocentricRadiusAu(Body body) {
    return static_cast<float>(g_rAu[bodyIndex(body)]);
}

float GetSceneDistance(Body body) {
    return GetOrbitRadius(body);
}

float GetAxialSpinDegrees(Body body) {
    // Earth's spin is a function of the epoch, not an accumulator: it is GMST at the
    // current UTC Julian date. That makes scrubbing the date reversible (rewinding puts
    // the terminator back exactly where it was) and makes the night side honest for a
    // given UTC, which the accumulator could not do — it only ever counted forward from
    // whenever the app happened to start.
    //
    // kEarthPrimeMeridianOffsetDeg is art, not astronomy: it lines the diffuse texture's
    // prime meridian up with the GMST zero point. It has not been calibrated against a
    // reference image, so treat Earth's absolute longitude under the terminator as
    // approximate; the rate and the epoch behaviour are the honest parts.
    //
    // Other bodies keep the accumulated spin — we have no measured prime-meridian epoch
    // for them in the catalog.
    if (body == Body::Earth) {
        return static_cast<float>(Ephemeris::GreenwichMeanSiderealTimeDeg(g_julianDate)) +
               kEarthPrimeMeridianOffsetDeg;
    }
    return g_axialSpinDeg[bodyIndex(body)];
}

float GetInclinationDegrees(Body body) {
    return kBodies[bodyIndex(body)].inclinationDeg;
}

glm::dvec3 GetHeliocentricScenePosition(Body body) {
    if (body == Body::Sun) {
        return glm::dvec3(0.0);
    }

    const int idx = bodyIndex(body);
    const double radius = orbitRadius(body);
    if (radius < 0.001) {
        return glm::dvec3(0.0);
    }

    const double lon = g_lonRad[idx];
    const double lat = g_latRad[idx];
    const double cosLat = std::cos(lat);
    // Scene Y-up: ecliptic x→X, ecliptic z→Y, ecliptic y→Z.
    return glm::dvec3(
        radius * std::cos(lon) * cosLat,
        radius * std::sin(lat),
        radius * std::sin(lon) * cosLat);
}

glm::vec3 GetOffset(Body body) {
    return glm::vec3(GetHeliocentricScenePosition(body));
}

glm::dvec3 GetRenderOrigin() {
    return g_renderOrigin;
}

void SetRenderOrigin(const glm::dvec3& origin) {
    g_renderOrigin = origin;
}

glm::vec3 ToRenderFrame(const glm::dvec3& heliocentricScene) {
    return glm::vec3(heliocentricScene - g_renderOrigin);
}

glm::dvec3 ToHeliocentricScene(const glm::vec3& renderFrame) {
    return glm::dvec3(renderFrame) + g_renderOrigin;
}

glm::vec3 GetRenderPosition(Body body) {
    return ToRenderFrame(GetHeliocentricScenePosition(body));
}

glm::dvec3 ChooseRenderOrigin(const glm::vec3& cameraRenderFrame) {
    if (g_scaleMode != ScaleMode::Realistic) {
        return glm::dvec3(0.0);
    }
    if (glm::length(cameraRenderFrame) <= kRenderOriginRebaseDistance) {
        return g_renderOrigin;
    }
    return ToHeliocentricScene(cameraRenderFrame);
}

Body BodyFromName(const std::string& name) {
    if (name == "Mercury") return Body::Mercury;
    if (name == "Venus") return Body::Venus;
    if (name == "Earth") return Body::Earth;
    if (name == "Mars") return Body::Mars;
    if (name == "Jupiter") return Body::Jupiter;
    if (name == "Saturn") return Body::Saturn;
    if (name == "Uranus") return Body::Uranus;
    if (name == "Neptune") return Body::Neptune;
    if (name == "Pluto") return Body::Pluto;
    if (name == "Ceres") return Body::Ceres;
    if (name == "Vesta") return Body::Vesta;
    if (name == "Eris") return Body::Eris;
    if (name == "Haumea") return Body::Haumea;
    if (name == "Makemake") return Body::Makemake;
    if (name == "Halley") return Body::Halley;
    return Body::Sun;
}

} // namespace OrbitLayout
