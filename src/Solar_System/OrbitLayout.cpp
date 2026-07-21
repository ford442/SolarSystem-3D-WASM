#include "OrbitLayout.h"

#include "Auxiliary_Modules/Ephemeris.h"

#include <algorithm>
#include <cmath>
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

// NASA fact-sheet averages for periods/inclination/sidereal day.
// Sidereal rotation days: Venus and Uranus are retrograde (negative).
constexpr BodyData kBodies[] = {
    // Sun
    {glm::vec3(0.0f), 0.0f, 0.0f, 0.0f, 25.38f},
    // Mercury
    {glm::vec3(1500.0f, 0.0f, 350.0f), 0.387f, 88.0f, 7.0f, 58.646f},
    // Venus
    {glm::vec3(1125.0f, 0.0f, -1340.0f), 0.723f, 224.7f, 3.4f, -243.025f},
    // Earth
    {glm::vec3(1900.0f, 0.0f, 0.0f), 1.0f, 365.25f, 0.0f, 0.99726968f},
    // Mars
    {glm::vec3(-1732.0f, 0.0f, 1000.0f), 1.524f, 687.0f, 1.9f, 1.025957f},
    // Jupiter
    {glm::vec3(1350.0f, 0.0f, 1737.0f), 5.203f, 4333.0f, 1.3f, 0.41354f},
    // Saturn
    {glm::vec3(0.0f, -100.0f, 2450.0f), 9.537f, 10759.0f, 2.5f, 0.44401f},
    // Uranus
    {glm::vec3(0.0f, 0.0f, -2650.0f), 19.191f, 30687.0f, 0.8f, -0.71833f},
    // Neptune
    {glm::vec3(-2900.0f, 0.0f, 0.0f), 30.069f, 60190.0f, 1.8f, 0.67125f},
    // Pluto
    {glm::vec3(2800.0f, 0.0f, 1757.73f), 39.482f, 90560.0f, 17.2f, 6.3872f},
};

constexpr int kBodyCount = static_cast<int>(sizeof(kBodies) / sizeof(kBodies[0]));

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
    return Body::Sun;
}

} // namespace OrbitLayout
