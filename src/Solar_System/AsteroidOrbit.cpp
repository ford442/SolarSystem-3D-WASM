#include "AsteroidOrbit.h"

#include "OrbitLayout.h"

#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace AsteroidOrbit {
namespace {

constexpr float kTwoPi = static_cast<float>(2.0 * M_PI);

class Rng {
public:
    explicit Rng(uint32_t seed) : _state(seed ? seed : 1u) {}

    uint32_t NextU32() {
        _state = _state * 1664525u + 1013904223u;
        return _state;
    }

    float Next01() {
        return static_cast<float>((NextU32() >> 8) & 0xFFFFFFu) / static_cast<float>(0x1000000u);
    }

    float NextRange(float lo, float hi) {
        return lo + (hi - lo) * Next01();
    }

private:
    uint32_t _state;
};

float DegToRad(float deg) {
    return deg * static_cast<float>(M_PI / 180.0);
}

} // namespace

const CometDef* GetCometDefs() {
    static const CometDef kDefs[kCometCount] = {
        {"1P/Halley", 0.586f, 0.967f, 162.3f, 111.3f, 58.4f, 38.4f, 75.3f, 1.8f, {0.75f, 0.82f, 0.95f}},
        {"2P/Encke", 0.336f, 0.848f, 11.8f, 186.5f, 334.6f, 120.0f, 3.3f, 1.2f, {0.85f, 0.78f, 0.65f}},
        {"C/1995 O1 Hale-Bopp", 0.914f, 0.995f, 89.4f, 130.6f, 282.5f, 210.0f, 2530.0f, 2.2f, {0.70f, 0.88f, 1.0f}},
    };
    return kDefs;
}

std::vector<OrbitalElements> GenerateBelt(uint32_t seed, int count) {
    count = std::clamp(count, 0, kMaxInstances);
    std::vector<OrbitalElements> out;
    out.reserve(static_cast<size_t>(count));

    Rng rng(seed);
    for (int i = 0; i < count; ++i) {
        OrbitalElements el;
        el.semiMajorAu = rng.NextRange(OrbitLayout::kMainBeltInnerAu, OrbitLayout::kMainBeltOuterAu);
        el.eccentricity = rng.NextRange(0.01f, 0.22f);
        el.inclinationRad = DegToRad(rng.NextRange(0.2f, 18.0f));
        el.argPeriapsisRad = rng.NextRange(0.0f, kTwoPi);
        el.longAscNodeRad = rng.NextRange(0.0f, kTwoPi);
        el.meanAnomalyRad = rng.NextRange(0.0f, kTwoPi);

        const float periodYears = std::pow(el.semiMajorAu, 1.5f);
        const float periodSeconds = OrbitLayout::kEarthOrbitSecondsAt1x * periodYears;
        el.meanMotionRadPerSec = kTwoPi / std::max(periodSeconds, 1.0f);

        const float sizeRoll = rng.Next01();
        el.visualRadius = (sizeRoll > 0.97f) ? rng.NextRange(1.2f, 2.4f)
                         : (sizeRoll > 0.85f) ? rng.NextRange(0.6f, 1.2f)
                                              : rng.NextRange(0.15f, 0.55f);
        el.spinPhaseRad = rng.NextRange(0.0f, kTwoPi);
        el.spinRateRadPerSec = rng.NextRange(-1.5f, 1.5f);

        const float shade = rng.NextRange(0.35f, 0.70f);
        const float tint = rng.NextRange(-0.05f, 0.08f);
        el.color = glm::vec3(shade + tint * 0.4f, shade + tint * 0.15f, shade - tint * 0.25f);

        out.push_back(el);
    }
    return out;
}

std::vector<OrbitalElements> GenerateComets() {
    std::vector<OrbitalElements> out;
    out.reserve(kCometCount);
    const CometDef* defs = GetCometDefs();
    for (int i = 0; i < kCometCount; ++i) {
        const CometDef& d = defs[i];
        OrbitalElements el;
        el.eccentricity = std::clamp(d.eccentricity, 0.0f, 0.999f);
        el.semiMajorAu = d.perihelionAu / std::max(1.0f - el.eccentricity, 1e-4f);
        el.inclinationRad = DegToRad(d.inclinationDeg);
        el.argPeriapsisRad = DegToRad(d.argPeriapsisDeg);
        el.longAscNodeRad = DegToRad(d.longAscNodeDeg);
        el.meanAnomalyRad = DegToRad(d.meanAnomaly0Deg);
        const float periodSeconds = OrbitLayout::kEarthOrbitSecondsAt1x * std::max(d.periodYears, 0.1f);
        el.meanMotionRadPerSec = kTwoPi / periodSeconds;
        el.visualRadius = d.nucleusRadius;
        el.spinPhaseRad = 0.0f;
        el.spinRateRadPerSec = 0.4f;
        el.color = d.color;
        out.push_back(el);
    }
    return out;
}

float SolveKepler(float meanAnomaly, float eccentricity) {
    float m = std::fmod(meanAnomaly, kTwoPi);
    if (m < 0.0f) {
        m += kTwoPi;
    }
    if (m > static_cast<float>(M_PI)) {
        m -= kTwoPi;
    }

    float eAnom = m;
    for (int i = 0; i < 8; ++i) {
        const float f = eAnom - eccentricity * std::sin(eAnom) - m;
        const float fp = 1.0f - eccentricity * std::cos(eAnom);
        eAnom -= f / fp;
    }
    return eAnom;
}

glm::vec3 OrbitalToCartesian(float radius, float trueAnomaly,
                             float inclination, float argPeriapsis, float longAscNode) {
    const float u = argPeriapsis + trueAnomaly;
    const float cosO = std::cos(longAscNode);
    const float sinO = std::sin(longAscNode);
    const float cosI = std::cos(inclination);
    const float sinI = std::sin(inclination);
    const float cosU = std::cos(u);
    const float sinU = std::sin(u);

    const float x = radius * (cosO * cosU - sinO * sinU * cosI);
    const float yEcl = radius * (sinO * cosU + cosO * sinU * cosI);
    const float zEcl = radius * (sinU * sinI);
    return glm::vec3(x, zEcl, yEcl);
}

float CurrentRadiusAu(const OrbitalElements& el) {
    const float e = std::clamp(el.eccentricity, 0.0f, 0.999f);
    const float eAnom = SolveKepler(el.meanAnomalyRad, e);
    return el.semiMajorAu * (1.0f - e * std::cos(eAnom));
}

glm::vec3 HeliocentricPosition(const OrbitalElements& el) {
    const float e = std::clamp(el.eccentricity, 0.0f, 0.999f);
    const float eAnom = SolveKepler(el.meanAnomalyRad, e);
    const float cosE = std::cos(eAnom);
    const float sinE = std::sin(eAnom);
    const float trueAnomaly = std::atan2(std::sqrt(1.0f - e * e) * sinE, cosE - e);
    const float rAu = el.semiMajorAu * (1.0f - e * cosE);
    const float rScene = OrbitLayout::AuToSceneDistance(rAu);
    return OrbitalToCartesian(rScene, trueAnomaly, el.inclinationRad, el.argPeriapsisRad, el.longAscNodeRad);
}

} // namespace AsteroidOrbit
