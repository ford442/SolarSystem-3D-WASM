#ifndef SOLARSYSTEM_SATELLITEORBIT_H
#define SOLARSYSTEM_SATELLITEORBIT_H

#include "../Auxiliary_Modules/Ephemeris.h"
#include "../SimState.h"
#include "BodyCatalog.generated.h"
#include "OrbitLayout.h"
#include <cmath>
#include <glm/glm.hpp>

namespace SatelliteOrbit {

constexpr float kEarthYearDays = 365.25f;
constexpr float kTwoPi = 6.28318530717958647692f;

/** Advance a mean anomaly using this frame's scaled sim delta. */
inline void AdvanceAnomaly(float& anomalyRad, float orbitalPeriodDays) {
    if (gSimState->simDeltaSeconds <= 0.0f || orbitalPeriodDays <= 0.0f) {
        return;
    }
    const float periodSeconds =
        OrbitLayout::kEarthOrbitSecondsAt1x * (orbitalPeriodDays / kEarthYearDays);
    anomalyRad += gSimState->simDeltaSeconds * (kTwoPi / periodSeconds);
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

/**
 * Parent-relative scene offset from the active ephemeris backend, for catalog satellites
 * that have a Keplerian solution (Moon, the Galileans, Titan, Triton — see
 * Ephemeris::IEphemeris::SatelliteParentRelative). False when the backend has none, in
 * which case the caller keeps Offset/OffsetXY.
 *
 * The orbit keeps its real shape and tilt but not its real size: the AU vector is scaled
 * so a body at its semi-major axis lands on the catalog sceneOrbitRadius, the same art
 * compression OrbitLayout applies to the planets. Axes follow OrbitLayout::GetOffset —
 * ecliptic x to scene X, ecliptic z to scene Y (scene is Y-up), ecliptic y to scene Z.
 */
inline bool EphemerisOffset(const BodyCatalog::Entry& entry, double julianDate,
                            glm::vec3& outOffset) {
    constexpr double kKmPerAu = 149597870.7;
    if (entry.keplerian.aKm <= 0.0f || entry.sceneOrbitRadius <= 0.0f) {
        return false;
    }

    double xyzAu[3] = {0.0, 0.0, 0.0};
    if (!Ephemeris::SatellitePosition(entry.index, julianDate, xyzAu)) {
        return false;
    }

    const double aAu = static_cast<double>(entry.keplerian.aKm) / kKmPerAu;
    const double toScene = static_cast<double>(entry.sceneOrbitRadius) / aAu;
    outOffset = glm::vec3(static_cast<float>(xyzAu[0] * toScene),
                          static_cast<float>(xyzAu[2] * toScene),
                          static_cast<float>(xyzAu[1] * toScene));
    return true;
}

/** Advance axial spin in degrees (linear in sim time). */
inline void AdvanceSpin(float& spinDegrees, float degreesPerSimSecond) {
    if (gSimState->simDeltaSeconds <= 0.0f) {
        return;
    }
    spinDegrees += degreesPerSimSecond * gSimState->simDeltaSeconds;
}

} // namespace SatelliteOrbit

#endif // SOLARSYSTEM_SATELLITEORBIT_H
