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

constexpr float kEarthYearDays = 365.25f;

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
float g_lonRad[kBodyCount] = {};
float g_latRad[kBodyCount] = {};
float g_axialSpinDeg[kBodyCount] = {};

int bodyIndex(Body body) {
    const int idx = static_cast<int>(body);
    return std::clamp(idx, 0, kBodyCount - 1);
}

void refreshEphemerisCache() {
    for (int i = 0; i < kBodyCount; ++i) {
        const Ephemeris::HelioLB pos = Ephemeris::Position(i, g_julianDate);
        g_lonRad[i] = static_cast<float>(pos.lonRad);
        g_latRad[i] = static_cast<float>(pos.latRad);
    }
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

float AuToSceneDistance(float au) {
    if (au <= 0.0f) {
        return 0.0f;
    }
    if (g_scaleMode == ScaleMode::Realistic) {
        return au * kAuToSceneUnits;
    }

    // Piecewise-linear remap through planet compressed orbit radii so the
    // main belt (and comets) sit between Mars and Jupiter in art scale.
    struct Key {
        float au;
        float scene;
    };
    const Key keys[] = {
        {GetAuDistance(Body::Mercury), glm::length(GetCompressedOffset(Body::Mercury))},
        {GetAuDistance(Body::Venus), glm::length(GetCompressedOffset(Body::Venus))},
        {GetAuDistance(Body::Earth), glm::length(GetCompressedOffset(Body::Earth))},
        {GetAuDistance(Body::Mars), glm::length(GetCompressedOffset(Body::Mars))},
        {GetAuDistance(Body::Jupiter), glm::length(GetCompressedOffset(Body::Jupiter))},
        {GetAuDistance(Body::Saturn), glm::length(GetCompressedOffset(Body::Saturn))},
        {GetAuDistance(Body::Uranus), glm::length(GetCompressedOffset(Body::Uranus))},
        {GetAuDistance(Body::Neptune), glm::length(GetCompressedOffset(Body::Neptune))},
        {GetAuDistance(Body::Pluto), glm::length(GetCompressedOffset(Body::Pluto))},
    };
    constexpr int keyCount = static_cast<int>(sizeof(keys) / sizeof(keys[0]));

    if (au <= keys[0].au) {
        return keys[0].scene * (au / keys[0].au);
    }
    for (int i = 0; i < keyCount - 1; ++i) {
        if (au <= keys[i + 1].au) {
            const float t = (au - keys[i].au) / (keys[i + 1].au - keys[i].au);
            return keys[i].scene + t * (keys[i + 1].scene - keys[i].scene);
        }
    }
    const Key& last = keys[keyCount - 1];
    return last.scene * (au / last.au);
}

float GetOrbitalPeriodDays(Body body) {
    return kBodies[bodyIndex(body)].orbitalPeriodDays;
}

float GetSiderealRotationDays(Body body) {
    return kBodies[bodyIndex(body)].siderealRotationDays;
}

float GetOrbitRadius(Body body) {
    if (body == Body::Sun) {
        return 0.0f;
    }
    const glm::vec3 compressed = GetCompressedOffset(body);
    if (g_scaleMode == ScaleMode::Compressed) {
        return glm::length(compressed);
    }
    return AuToSceneDistance(GetAuDistance(body));
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

glm::vec3 GetOffset(Body body) {
    if (body == Body::Sun) {
        return glm::vec3(0.0f);
    }

    const int idx = bodyIndex(body);
    const float radius = GetOrbitRadius(body);
    if (radius < 0.001f) {
        return glm::vec3(0.0f);
    }

    const float lon = g_lonRad[idx];
    const float lat = g_latRad[idx];
    const float cosLat = std::cos(lat);
    // Scene Y-up: ecliptic x→X, ecliptic z→Y, ecliptic y→Z.
    return glm::vec3(
        radius * std::cos(lon) * cosLat,
        radius * std::sin(lat),
        radius * std::sin(lon) * cosLat);
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
    return Body::Sun;
}

} // namespace OrbitLayout
