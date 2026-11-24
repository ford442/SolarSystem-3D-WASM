#ifndef SOLARSYSTEM_ORBITLAYOUT_H
#define SOLARSYSTEM_ORBITLAYOUT_H

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <string>
#include <vector>

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

/**
 * Number of focus bodies in *Sol's* table (Sun … 1P/Halley): the frozen focus indices of
 * deep links and the FocusPlanet export. Another star system has its own count — use
 * BodyCount() for "every body of the scene", kBodyCount only where Sol is meant.
 */
constexpr int kBodyCount = static_cast<int>(Body::Halley) + 1;

/**
 * Star-centric Keplerian elements (system reference plane, epoch J2000) that place a body
 * whose system has no analytic ephemeris — every system but Sol. aAu == 0: none.
 */
struct KeplerElements {
    double aAu = 0.0;
    double e = 0.0;
    double iDeg = 0.0;
    double OmegaDeg = 0.0;
    double omegaDeg = 0.0;
    double M0Deg = 0.0;
    double nDegPerDay = 0.0;
};

/**
 * One focus body of the active system's layout table (row i is focus index i; row 0 the star).
 * Sol's rows are compiled from OrbitLayoutBodies.generated.inc; another system's come from its
 * runtime catalog (StarSystemCatalog), with `kepler` set.
 */
struct BodyRow {
    glm::vec3 compressedOffset{0.0f};
    float auDistance = 0.0f;
    float orbitalPeriodDays = 0.0f;
    float inclinationDeg = 0.0f;
    float siderealRotationDays = 0.0f; // negative => retrograde
    // IAU prime-meridian angle at J2000 and its rate: W = W0 + Wdot * days. Double, since
    // Wdot times a century of days is millions of degrees. Negative rate => retrograde.
    double primeMeridianDegJ2000 = 0.0;
    double primeMeridianRateDegPerDay = 0.0;
    KeplerElements kepler{};
    /** Placed at its ephemeris distance on compressed scale too (Sol: Eris … Halley). */
    bool trueRadius = false;
};

/**
 * Make `rows[0 … count-1]` the layout table (another star system), or nullptr for Sol's
 * compiled one. Refreshes positions for the current date; the render origin is kept (the
 * caller moves the camera). StarSystemCatalog::Activate calls this; `rows` must outlive it.
 */
void SetSystemRows(const BodyRow* rows, int count);

/** Focus bodies in the active system (star included): valid Body values are 0 … BodyCount()-1. */
int BodyCount();

/** True while the layout is Sol's (Standish ephemeris, Earth's solved spin, the art keys). */
bool IsSolLayout();

enum class ScaleMode : int {
    Compressed = 0,
    Realistic = 1
};

/** Wall seconds for one Earth orbit at timeScale == 1. */
constexpr float kEarthOrbitSecondsAt1x = 120.0f;
/** Days per Earth orbit, the other half of the orrery-seconds ↔ days conversion. */
constexpr float kEarthYearDays = 365.25f;

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

/** Advance the ephemeris epoch by scaled wall-clock seconds (spins follow from the date). */
void Advance(float scaledDtSeconds);

/** Reset to J2000.0, compressed scale, render origin at the Sun, Sol's table (tests). */
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
// The simulation origin is the Sun, but the frame the GPU sees is centred on the camera
// (a floating origin, docs/ARCHITECTURE.md §11.1) so that float positions near the viewer
// are as fine-grained as a float gets. Every glm::vec3 scene position in the engine
// (Transformable::GetPosition, the Sun's own position, uniforms, model matrices) is in this
// render frame:
//
//     render = float(heliocentricScene - renderOrigin)
//
// computed in double. Application::MoveRenderOrigin puts the origin on the camera's world
// position once per frame, in both scale modes, after everything that moves the camera;
// the view matrix then has no translation.

glm::dvec3 GetRenderOrigin();
void SetRenderOrigin(const glm::dvec3& origin);

/** Heliocentric scene position (double) → render frame (float), subtracting in double. */
glm::vec3 ToRenderFrame(const glm::dvec3& heliocentricScene);

/** Render frame (float) → heliocentric scene position (double). */
glm::dvec3 ToHeliocentricScene(const glm::vec3& renderFrame);

/** GetHeliocentricScenePosition(body) in the render frame. The Sun's is -origin. */
glm::vec3 GetRenderPosition(Body body);

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
 * The (AU, scene) knots AuToSceneDistanceDouble interpolates through, increasing in AU, and
 * proportional below the first and past the last: one knot at 1 AU on realistic scale. For a
 * shader that has to place AU positions the way the scene does (common/space_weather.glsl).
 */
std::vector<glm::dvec2> AuToSceneKnots();

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

/** GetOrbitRadius in double: the radius the body is drawn at (the flight planner pins craft to it). */
double GetOrbitRadiusDouble(Body body);

/** True for the eccentric outer-system bodies (Eris … Halley) placed at their real r. */
bool UsesTrueRadius(Body body);

/** Heliocentric distance in AU at the current Julian date (from the ephemeris cache). */
float GetHeliocentricRadiusAu(Body body);

/** Alias of GetOrbitRadius — scene distance from the Sun. */
float GetSceneDistance(Body body);

/** Compressed-scene offset baked into the original art direction (phase-0 reference). */
glm::vec3 GetCompressedOffset(Body body);

/**
 * Axial spin in degrees at `julianDate`, wrapped to [0, 360): the turn about the body's pole
 * that CatalogBody applies after the tilt. A pure function of the date (no state), in double
 * so a caller can difference it frame to frame without float's ~3e-5° grain, which is 3 m at
 * Earth's surface. Earth: solved so the drawn map has the true sub-solar longitude. Others:
 * IAU W0 + Wdot * days since J2000 from the catalog (negative rate = retrograde).
 */
double AxialSpinDegreesAt(Body body, double julianDate);

/** AxialSpinDegreesAt at the current Julian date, as the float the model matrix takes. */
float GetAxialSpinDegrees(Body body);

/** Orbital period in Earth days (NASA averages). Sun returns 0. */
float GetOrbitalPeriodDays(Body body);

/** Sidereal rotation period in Earth days; negative for retrograde rotators. */
float GetSiderealRotationDays(Body body);

/** Orbital inclination in degrees (NASA averages). */
float GetInclinationDegrees(Body body);

/** Map wall-scaled seconds for one full orbit of this body at timeScale==1. */
float GetOrbitPeriodSecondsAt1x(Body body);

/**
 * Map manifest / UI name to body id; returns Sun (the star, index 0) for unknown names. Sol's
 * names are fixed; another system's are its catalog display names and ids.
 */
Body BodyFromName(const std::string& name);

} // namespace OrbitLayout

#endif // SOLARSYSTEM_ORBITLAYOUT_H
