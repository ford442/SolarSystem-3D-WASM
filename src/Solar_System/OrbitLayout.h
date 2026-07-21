#ifndef SOLARSYSTEM_ORBITLAYOUT_H
#define SOLARSYSTEM_ORBITLAYOUT_H

#include <glm/vec3.hpp>
#include <string>

// Canonical planet indices match FocusPlanet / planet_facts.json (0=Sun … 9=Pluto).
namespace OrbitLayout {

enum class Body : int {
    Sun = 0,
    Mercury = 1,
    Venus = 2,
    Earth = 3,
    Mars = 4,
    Jupiter = 5,
    Saturn = 6,
    Uranus = 7,
    Neptune = 8,
    Pluto = 9
};

enum class ScaleMode : int {
    Compressed = 0,
    Realistic = 1
};

/** Wall seconds for one Earth orbit at timeScale == 1. */
constexpr float kEarthOrbitSecondsAt1x = 120.0f;

/** 1 AU in scene units (matches Earth's compressed orbit radius). */
constexpr float kAuToSceneUnits = 1900.0f;

/** Main-belt semi-major axis range in AU (approximate). */
constexpr float kMainBeltInnerAu = 2.1f;
constexpr float kMainBeltOuterAu = 3.3f;

void SetScaleMode(ScaleMode mode);
ScaleMode GetScaleMode();

/** Set the simulation epoch (Julian Date) and refresh ephemeris positions. */
void SetJulianDate(double julianDate);
double GetJulianDate();

/** Advance ephemeris epoch and axial spin by scaled wall-clock seconds. */
void Advance(float scaledDtSeconds);

/** Reset to J2000.0 and zero axial spin (tests / deterministic restarts). */
void ResetForTests();

/**
 * Current heliocentric offset from the Sun in scene units for the active scale mode.
 * Direction from Standish/Keplerian ecliptic lon/lat at the current Julian date;
 * radius from GetOrbitRadius (compressed art length or AU-scaled).
 */
glm::vec3 GetOffset(Body body);

/** Semi-major axis in astronomical units (NASA fact-sheet averages). */
float GetAuDistance(Body body);

/**
 * Map a heliocentric AU distance to scene units for the active scale mode.
 * Realistic: au * kAuToSceneUnits.
 * Compressed: piecewise-linear through planet art orbit radii.
 */
float AuToSceneDistance(float au);

/** Orbit radius in scene units (scale-mode aware; independent of anomaly). */
float GetOrbitRadius(Body body);

/** Alias of GetOrbitRadius — scene distance from the Sun. */
float GetSceneDistance(Body body);

/** Compressed-scene offset baked into the original art direction (phase-0 reference). */
glm::vec3 GetCompressedOffset(Body body);

/** Accumulated axial spin in degrees (linear in sim time; negative = retrograde). */
float GetAxialSpinDegrees(Body body);

/** Orbital period in Earth days (NASA averages). Sun returns 0. */
float GetOrbitalPeriodDays(Body body);

/** Sidereal rotation period in Earth days; negative for retrograde rotators. */
float GetSiderealRotationDays(Body body);

/** Orbital inclination in degrees (NASA averages). */
float GetInclinationDegrees(Body body);

/** Map wall-scaled seconds for one full orbit of this body at timeScale==1. */
float GetOrbitPeriodSecondsAt1x(Body body);

/** Map manifest / UI name to body id; returns Sun for unknown names. */
Body BodyFromName(const std::string& name);

} // namespace OrbitLayout

#endif // SOLARSYSTEM_ORBITLAYOUT_H
