#ifndef SOLARSYSTEM_SATELLITEORBIT_H
#define SOLARSYSTEM_SATELLITEORBIT_H

#include "../SimState.h"
#include "OrbitLayout.h"
#include <cmath>
#include <glm/glm.hpp>

namespace SatelliteOrbit {

constexpr float kEarthYearDays = 365.25f;
constexpr float kTwoPi = 6.28318530717958647692f;

/** Advance a mean anomaly using this frame's scaled sim delta. */
inline void AdvanceAnomaly(float& anomalyRad, float orbitalPeriodDays) {
    if (gSimDeltaSeconds <= 0.0f || orbitalPeriodDays <= 0.0f) {
        return;
    }
    const float periodSeconds =
        OrbitLayout::kEarthOrbitSecondsAt1x * (orbitalPeriodDays / kEarthYearDays);
    anomalyRad += gSimDeltaSeconds * (kTwoPi / periodSeconds);
    anomalyRad = std::fmod(anomalyRad, kTwoPi);
    if (anomalyRad < 0.0f) {
        anomalyRad += kTwoPi;
    }
}

/** Parent-relative offset on a circular equatorial (XZ) orbit. */
inline glm::vec3 Offset(float radius, float anomalyRad) {
    return glm::vec3(radius * std::cos(anomalyRad), 0.0f, radius * std::sin(anomalyRad));
}

/** Parent-relative offset on a circular polar (XY) orbit — Uranian moons. */
inline glm::vec3 OffsetXY(float radius, float anomalyRad) {
    return glm::vec3(radius * std::cos(anomalyRad), radius * std::sin(anomalyRad), 0.0f);
}

/** Advance axial spin in degrees (linear in sim time). */
inline void AdvanceSpin(float& spinDegrees, float degreesPerSimSecond) {
    if (gSimDeltaSeconds <= 0.0f) {
        return;
    }
    spinDegrees += degreesPerSimSecond * gSimDeltaSeconds;
}

} // namespace SatelliteOrbit

#endif // SOLARSYSTEM_SATELLITEORBIT_H
