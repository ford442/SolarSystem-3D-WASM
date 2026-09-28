#ifndef SOLARSYSTEM_SATELLITEORBIT_H
#define SOLARSYSTEM_SATELLITEORBIT_H

#include "../Auxiliary_Modules/Ephemeris.h"
#include "BodyCatalog.generated.h"
#include "OrbitLayout.h"
#include <cmath>
#include <glm/glm.hpp>

namespace SatelliteOrbit {

constexpr float kEarthYearDays = 365.25f;
constexpr float kTwoPi = 6.28318530717958647692f;

/** Sim-seconds (OrbitLayout's 120 s = one Earth year clock) elapsed from J2000 to julianDate. */
inline double SimSecondsSinceJ2000(double julianDate) {
    return (julianDate - Ephemeris::kJ2000) *
           (static_cast<double>(OrbitLayout::kEarthOrbitSecondsAt1x) / kEarthYearDays);
}

/**
 * Circular-orbit mean anomaly at julianDate: initialAnomalyRad at J2000, one turn per
 * orbitalPeriodDays. A pure function of the date — like OrbitLayout's planets — so a
 * date-scrubber jump lands every moon where that date puts it, and scrubbing back returns
 * it exactly. Absolute phase is still art for rows whose M0Deg is a placeholder.
 */
inline float MeanAnomalyAt(const BodyCatalog::Entry& entry, double julianDate) {
    if (entry.orbitalPeriodDays <= 0.0f) {
        return entry.initialAnomalyRad;
    }
    // Wrap the turn count in double before narrowing: decades of Mimas are ~10^4 turns.
    const double turns = (julianDate - Ephemeris::kJ2000) / entry.orbitalPeriodDays;
    const double fraction = turns - std::floor(turns);
    double anomaly = entry.initialAnomalyRad + 2.0 * 3.14159265358979323846 * fraction;
    anomaly = std::fmod(anomaly, 2.0 * 3.14159265358979323846);
    if (anomaly < 0.0) {
        anomaly += 2.0 * 3.14159265358979323846;
    }
    return static_cast<float>(anomaly);
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

/** Axial spin in degrees at julianDate (0 at J2000), wrapped to [0, 360). */
inline float SpinDegreesAt(float degreesPerSimSecond, double julianDate) {
    double spin = std::fmod(static_cast<double>(degreesPerSimSecond) * SimSecondsSinceJ2000(julianDate), 360.0);
    if (spin < 0.0) {
        spin += 360.0;
    }
    return static_cast<float>(spin);
}

} // namespace SatelliteOrbit

#endif // SOLARSYSTEM_SATELLITEORBIT_H
