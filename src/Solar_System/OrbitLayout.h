#ifndef SOLARSYSTEM_ORBITLAYOUT_H
#define SOLARSYSTEM_ORBITLAYOUT_H

#include <glm/vec3.hpp>
#include <string>

// Canonical body indices match FocusPlanet / planet_facts.json (0=Sun … 9=Pluto,
// then catalog-only dwarf/belt bodies).
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
    Pluto = 9,
    Ceres = 10,
    Vesta = 11,
    // Outer system (catalog-only). New focus bodies append here; never renumber 0–11.
    Eris = 12,
    Haumea = 13,
    Makemake = 14,
    Halley = 15
};

/** Number of bodies in the layout table; valid Body values are 0 … kBodyCount-1. */
constexpr int kBodyCount = static_cast<int>(Body::Halley) + 1;

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

/** Classical Kuiper-belt semi-major axis range in AU (inside the 2:1 resonance at ~48). */
constexpr float kKuiperBeltInnerAu = 30.0f;
constexpr float kKuiperBeltOuterAu = 50.0f;

void SetScaleMode(ScaleMode mode);
ScaleMode GetScaleMode();

/** Set the simulation epoch (Julian Date) and refresh ephemeris positions. */
void SetJulianDate(double julianDate);
double GetJulianDate();

/** Advance ephemeris epoch and axial spin by scaled wall-clock seconds. */
void Advance(float scaledDtSeconds);

/** Reset to J2000.0, zero axial spin, compressed scale, render origin at the Sun (tests). */
void ResetForTests();

/**
 * Current heliocentric offset from the Sun in scene units for the active scale mode.
 * Direction from Standish/Keplerian ecliptic lon/lat at the current Julian date;
 * radius from GetOrbitRadius (compressed art length or AU-scaled).
 *
 * Float, so only for coarse callers (nearest-planet search, activation radii). On realistic
 * scale a float at Neptune's distance is ~0.004 scene units coarse, which is a tenth of
 * Earth's realistic radius: anything that becomes a model matrix or a uniform must go
 * through GetRenderPosition instead.
 */
glm::vec3 GetOffset(Body body);

/** GetOffset in double: the heliocentric scene position before any float rounding. */
glm::dvec3 GetHeliocentricScenePosition(Body body);

// --- Render origin -----------------------------------------------------------------------
//
// The simulation origin is the Sun, but the frame the GPU sees is centred on a render
// origin near the camera so that the float positions near the viewer stay fine-grained.
// Every glm::vec3 scene position in the engine (Transformable::GetPosition, the camera,
// the Sun's own position, uniforms, model matrices) is in this render frame:
//
//     render = float(heliocentricScene - renderOrigin)
//
// computed in double. The origin only moves when the camera strays more than
// kRenderOriginRebaseDistance from it (ChooseRenderOrigin), and on compressed scale it
// stays pinned at the Sun, so the render frame is the heliocentric frame there.

/**
 * How far (scene units) the camera may stray from the render origin before it rebases.
 * Floats below 512 are spaced at most 6e-5 units apart, under a thousandth of Earth's
 * realistic radius (~0.08), so a body one Earth radius away is placed to well under a pixel.
 * At the top fly speed (150 units/s) that is a rebase every few seconds, not every frame;
 * only a focus transition across the system rebases on most frames for its two seconds.
 */
constexpr float kRenderOriginRebaseDistance = 512.0f;

glm::dvec3 GetRenderOrigin();
void SetRenderOrigin(const glm::dvec3& origin);

/** Heliocentric scene position (double) → render frame (float), subtracting in double. */
glm::vec3 ToRenderFrame(const glm::dvec3& heliocentricScene);

/** Render frame (float) → heliocentric scene position (double). */
glm::dvec3 ToHeliocentricScene(const glm::vec3& renderFrame);

/** GetHeliocentricScenePosition(body) in the render frame. The Sun's is -origin. */
glm::vec3 GetRenderPosition(Body body);

/**
 * The origin the render frame should use given where the camera is in it. Compressed:
 * always the Sun. Realistic: the current origin until the camera is more than
 * kRenderOriginRebaseDistance from it, then the camera's own heliocentric position.
 */
glm::dvec3 ChooseRenderOrigin(const glm::vec3& cameraRenderFrame);

/** Semi-major axis in astronomical units (NASA fact-sheet averages). */
float GetAuDistance(Body body);

/**
 * Map a heliocentric AU distance to scene units for the active scale mode.
 * Realistic: au * kAuToSceneUnits.
 * Compressed: piecewise-linear through planet art orbit radii.
 */
float AuToSceneDistance(float au);

/** AuToSceneDistance in double. */
double AuToSceneDistanceDouble(double au);

/**
 * Map a heliocentric AU vector in scene axes (ecliptic x,z,y) to scene units.
 * Preserves direction; length uses AuToSceneDistance so compressed/realistic both work.
 */
glm::vec3 HelioAuToScene(const glm::vec3& auSceneAxes);

/** HelioAuToScene in double, for positions that will go through ToRenderFrame. */
glm::dvec3 HelioAuToSceneDouble(const glm::dvec3& auSceneAxes);

/**
 * Orbit radius in scene units (scale-mode aware). Independent of anomaly for Mercury–Vesta,
 * which keep their art/mean radius; outer-system bodies (UsesTrueRadius) follow the
 * ephemeris distance so Eris and Halley swing in and out along their real ellipses.
 */
float GetOrbitRadius(Body body);

/** True for the eccentric outer-system bodies (Eris … Halley) placed at their real r. */
bool UsesTrueRadius(Body body);

/** Heliocentric distance in AU at the current Julian date (from the ephemeris cache). */
float GetHeliocentricRadiusAu(Body body);

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
