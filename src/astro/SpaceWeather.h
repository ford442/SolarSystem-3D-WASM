#ifndef SOLARSYSTEM_ASTRO_SPACEWEATHER_H
#define SOLARSYSTEM_ASTRO_SPACEWEATHER_H
#include "../Solar_System/OrbitLayout.h"
#include <cstddef>
#include <deque>
#include <glm/vec3.hpp>
#include <string>
#include <vector>

namespace Json {
class Value;
}
namespace BodyCatalog {
struct StarRow;
}

/**
 * Space weather, GL-free (docs/ARCHITECTURE.md §13): the solar wind's pressure on each
 * magnetosphere, the auroral ovals it drives, the Sun's sunspot cycle and the CMEs that sweep
 * the system. Everything is a function of the date (plus the Kp override and any CME launched
 * by hand), so a date jump or a capture lands on the same state.
 *
 * The particle stream (render/SolarWindRenderer) is the picture; these numbers are the model.
 * Kp comes from here, not from counting the particles that reach the cusps on the GPU: a
 * readback would stall the frame and tie Kp to the quality tier's particle count.
 *
 * Distances are true ones (AU, km, planet radii). The renderer maps them into the scene.
 */
namespace SpaceWeather {

// --- Constants ------------------------------------------------------------------------------

constexpr double kMu0 = 1.25663706212e-6; // vacuum permeability, T·m/A
constexpr double kProtonMassKg = 1.67262192369e-27;
constexpr double kAuKm = 149597870.7;
constexpr double kSunRadiusKm = 695700.0;
constexpr double kSecondsPerDay = 86400.0;
/** Carrington (sidereal) rotation of the Sun: what winds the field into the Parker spiral. */
constexpr double kSolarRotationDays = 25.38;
constexpr double kSolarRotationRadPerSecond = 6.283185307179586 / (kSolarRotationDays * kSecondsPerDay);
/** Quiet slow wind at 1 AU (OMNI medians round to these). Density falls as 1/r². */
constexpr double kQuietDensityAt1AuPerCm3 = 5.0;
constexpr double kSlowWindKmS = 400.0;
/** Wind from coronal holes: the polar wind near minimum and the recurrent high-speed streams. */
constexpr double kFastWindKmS = 750.0;
/**
 * The field just inside the magnetopause is the dipole's doubled by the Chapman–Ferraro
 * currents, so pressure balance reads (2·B)²/2μ₀ = ρv². With a factor 1 the same formula gives
 * ~8 R_E for Earth instead of the observed ~10.
 */
constexpr double kBoundaryFieldCompression = 2.0;
/** Kp of the quiet magnetosphere the coupling is measured against (the ticket's baseline). */
constexpr double kQuietKp = 2.0;
/** Kp per doubling of the coupling (fits Kp 9 at Halloween 2003, ~6 for a 700 km/s CME). */
constexpr double kKpPerDoubling = 1.25;

// --- Pressure balance -----------------------------------------------------------------------

/** Solar-wind dynamic pressure m_p·n·v², in nPa, for n in cm⁻³ and v in km/s. */
double DynamicPressureNPa(double densityPerCm3, double speedKmS);

/**
 * Subsolar magnetopause distance in planet radii from pressure balance,
 * r = inflation · ((f·B₀)² / (2μ₀ · P))^(1/6), with f = kBoundaryFieldCompression and B₀ the
 * dipole's equatorial surface field. `inflation` stands for what a dipole balance leaves out:
 * the plasma disc that Io (Jupiter) and Enceladus (Saturn) load into the magnetosphere.
 */
double StandoffRadii(double surfaceFieldTesla, double dynamicPressureNPa, double inflation = 1.0);

/** Shue et al. (1997) flaring exponent α of r(θ) = r₀ (2 / (1 + cos θ))^α, for Bz = 0. */
double ShueFlaringExponent(double dynamicPressureNPa);

/** Magnetopause distance at angle θ from the subsolar direction (Shue et al. 1997). */
double MagnetopauseRadius(double standoff, double flaringExponent, double cosTheta);

/** Quiet slow-wind dynamic pressure at `rAu` (kQuietDensityAt1AuPerCm3 / r², kSlowWindKmS). */
double QuietPressureNPa(double rAu);

// --- The Parker spiral ----------------------------------------------------------------------

/**
 * Angle between the interplanetary field and the radial direction, tan ψ = Ω r cos λ / v:
 * ~45° at Earth for a 400 km/s wind, nearly azimuthal by Jupiter.
 */
double ParkerSpiralAngleRad(double rAu, double speedKmS, double heliolatitudeRad = 0.0);

// --- Auroral ovals --------------------------------------------------------------------------

/** Oval boundaries in magnetic latitude, degrees, at magnetic midnight and noon. */
struct Oval {
    double equatorwardMidnightDeg = 0.0;
    double equatorwardNoonDeg = 0.0;
    double polewardMidnightDeg = 0.0;
    double polewardNoonDeg = 0.0;
};

/** Each boundary as `latitude at Kp 0 + slope · Kp` (degrees, degrees per Kp). */
struct OvalModel {
    double equatorwardMidnight0, equatorwardMidnightPerKp;
    double equatorwardNoon0, equatorwardNoonPerKp;
    double polewardMidnight0, polewardMidnightPerKp;
    double polewardNoon0, polewardNoonPerKp;
};

Oval OvalAt(const OvalModel& model, double kp);

/**
 * Earth's oval after Feldstein & Starkov (1967) / Starkov (1994), linearised: the night-side
 * equatorward edge sits near 66° at Kp 0 and comes down ~2° per Kp, to ~48° at Kp 9.
 */
Oval EarthOval(double kp);

/**
 * Equatorward boundary at magnetic local time angle `mltRad` (0 = magnetic noon, π = midnight),
 * blending the noon and midnight values with a cosine.
 */
double EquatorwardLatitudeDeg(const Oval& oval, double mltRad);
double PolewardLatitudeDeg(const Oval& oval, double mltRad);

// --- Coupling and Kp ------------------------------------------------------------------------

/**
 * Energy the wind couples into a magnetosphere relative to quiet: the intercepted kinetic flux
 * ρv³·π r_mp² normalised by its quiet value. With r_mp ∝ P^(-1/6) that is
 * (P/P₀)^(2/3) · (v/v₀). `geoeffectiveness` multiplies the part a CME brings (a southward
 * field couples far more than the pressure alone says); 1 for ambient wind.
 */
double CouplingRatio(double pressureNPa, double quietPressureNPa, double speedKmS, double geoeffectiveness = 1.0);

/** Kp before clamping: kQuietKp + kKpPerDoubling · log2(coupling). */
double KpFromCouplingUnclamped(double couplingRatio);
/** The same, clamped to the index's 0–9. */
double KpFromCoupling(double couplingRatio);

// --- The sunspot cycle ----------------------------------------------------------------------

/** render.star.activity.cycle: an 11-year cycle from the 2008.9 minimum for the Sun. */
struct CycleParams {
    double periodYears = 0.0; // 0: the star has no cycle (no spots drawn)
    double minimumYear = 2008.9;
    int referenceCycle = 24;      // the cycle that starts at minimumYear
    double meanAmplitude = 179.0; // peak smoothed sunspot number of a cycle with no table entry
    bool solarAmplitudes = false; // use the Sun's observed cycle maxima (SolarCycleAmplitude)
};

/** A star's cycle from its catalog row (render.star.activity.cycle); periodYears 0 if none. */
CycleParams CycleFromStar(const BodyCatalog::StarRow& star);
/** The Sun's: CycleFromStar(BodyCatalog::kSolStar), an 11-year cycle from the 2008.9 minimum. */
CycleParams SolarCycle();

/** One butterfly-diagram wing: where the spots of one cycle are, and how many. */
struct SpotBand {
    double latitudeDeg = 0.0; // centroid, both hemispheres (Spörer's law)
    double halfWidthDeg = 0.0;
    double coverage = 0.0; // 0–1: that cycle's sunspot number / kFullCoverageSunspots
};
constexpr double kFullCoverageSunspots = 250.0;

struct CycleState {
    int cycleNumber = 0;
    double phase = 0.0; // 0–1 since this cycle's minimum
    double yearsSinceMinimum = 0.0;
    double sunspotNumber = 0.0; // smoothed total of this cycle and the previous one's tail
    SpotBand current;           // this cycle's band
    SpotBand previous;          // the previous cycle's low-latitude tail
};

/** Julian date ↔ decimal year (Julian years from J2000; within hours of the civil year). */
double DecimalYear(double julianDate);
double JulianDateFromDecimalYear(double year);

/**
 * Years from minimum to maximum for a cycle peaking at `amplitude` (the Waldmeier effect: 4.5
 * for a mean cycle, longer for weak ones), scaled to the period.
 */
double CycleRiseYears(double amplitude, double periodYears);

/**
 * Smoothed sunspot number of one cycle `yearsSinceMinimum` into it, peaking at `amplitude`
 * CycleRiseYears after minimum: Hathaway, Wilson & Reichmann's (1994) shape
 * a·t³/(exp(t²/b²) − 0.71).
 */
double CycleShape(double yearsSinceMinimum, double amplitude, double periodYears);

/** Peak smoothed sunspot number of solar cycle `number` (SILSO v2), or `fallback`. */
double SolarCycleAmplitude(int number, double fallback);

/** Spörer's law: the spot band's centroid latitude, 28° · exp(−t / 90 months). */
double SporerLatitudeDeg(double yearsSinceMinimum);

CycleState EvaluateCycle(const CycleParams& params, double julianDate);

// --- The wind -------------------------------------------------------------------------------

struct Wind {
    double densityPerCm3 = 0.0;
    double speedKmS = 0.0;
    double pressureNPa = 0.0;
    double cmeStrength = 0.0;      // 0–1: how deep inside a CME this point is
    double geoeffectiveness = 1.0; // of the strongest CME here
    int cme = -1;                  // into the active list; -1 none
};

/** Half-width of the slow wind's streamer belt in heliographic latitude at `cyclePhase`. */
double StreamerBeltHalfWidthDeg(double cyclePhase);

/**
 * Ambient wind at `rAu` and heliographic latitude, `sourceLongitudeRad` being the corotating
 * longitude on the Sun the parcel left from (EvaluateAmbient computes it for a body). The slow
 * wind fills the streamer belt; fast streams from coronal holes reach the ecliptic twice a
 * rotation, strongest in the declining phase, and fill the high latitudes near minimum.
 */
Wind AmbientWind(double rAu, double heliolatitudeRad, double sourceLongitudeRad, double cyclePhase);

/**
 * The source longitude a parcel at ecliptic longitude `longitudeRad` and distance `rAu` left,
 * in the Sun's rotating frame: L − Ω t + Ω r / v (the spiral's lag).
 */
double SourceLongitudeRad(double longitudeRad, double rAu, double speedKmS, double julianDate);

// --- CMEs -----------------------------------------------------------------------------------

/** One event of resource/spaceweather/cmes.json, or a CME launched by hand. */
struct Cme {
    std::string id;
    std::string name;
    double launchJd = 0.0;
    double launchRadiusKm = kSunRadiusKm; // where `launchJd` has the front (21.5 R☉ for DONKI)
    double speedKmS = 1000.0;             // initial speed
    double halfAngleRad = 0.785;
    double heeqLongitudeRad = 0.0; // Stonyhurst: 0 faces Earth at launch, + is west
    double heeqLatitudeRad = 0.0;
    double observedArrivalJd = 0.0; // shock at Earth, 0 unknown
    double kpMax = -1.0;            // observed peak Kp, < 0 unknown
    std::string source;             // "curated", "donki", "user"

    double geoeffectiveness = 1.0; // a hand-launched CME sets it; Calibrate fits catalog ones

    // Derived by Calibrate().
    double modelSpeedKmS = 1000.0; // initial speed the model runs (speedKmS unless no drag fits)
    double dragPerKm = 2.0e-8;     // drag-based model γ
    double ambientSpeedKmS = kSlowWindKmS;
    double modelArrivalJd = 0.0;         // when the front reaches Earth's distance; 0 never
    double modelKpMax = -1.0;            // peak Kp at Earth with the fitted geoeffectiveness
    bool hitsEarth = false;              // Earth is inside the cone when the front arrives
    glm::dvec3 direction{1.0, 0.0, 0.0}; // scene axes (ecliptic x, north, ecliptic y)

    /** The observed arrival if known, else the modelled one (0 when it misses Earth). */
    double ArrivalAtEarthJd() const;
    /** The observed peak Kp if known, else the modelled one (-1 when it misses Earth). */
    double PeakKp() const;
};

/**
 * Drag-based model (Vršnak et al. 2013): dv/dt = −γ (v − w)|v − w| toward the ambient speed w.
 * Front distance in km `seconds` after launch, and its speed.
 */
double DragFrontKm(double speedKmS, double ambientKmS, double dragPerKm, double launchRadiusKm, double seconds);
double DragSpeedKmS(double speedKmS, double ambientKmS, double dragPerKm, double seconds);

double FrontKm(const Cme& cme, double julianDate);
double FrontSpeedKmS(const Cme& cme, double julianDate);
/** When the front reaches `distanceKm` (bisection on the monotone front), or 0 if never. */
double ArrivalJd(const Cme& cme, double distanceKm);

/** Self-similar shell: the sheath and ejecta fill `kShellThickness` of the front's radius (an ICME takes about a day to pass Earth). */
constexpr double kShellThickness = 0.45;
/** Fraction past the cone's half-angle the shock's flank still reaches. */
constexpr double kFlankReach = 1.15;

/**
 * How deep `direction` at `rAu` sits in the CME at `julianDate`: 0 outside, peaking just
 * behind the shock and falling off toward the cone's edge.
 */
double CmeStrengthAt(const Cme& cme, const glm::dvec3& direction, double rAu, double julianDate);

/** The wind at a point with every CME in `cmes` laid over the ambient wind. */
Wind WindAt(const Wind& ambient, const std::vector<const Cme*>& cmes, const glm::dvec3& direction, double rAu,
            double julianDate);

/** Scene axes (ecliptic x, north, ecliptic y) of a heliocentric ecliptic longitude/latitude. */
glm::dvec3 SceneDirection(double eclipticLongitudeRad, double eclipticLatitudeRad);

/**
 * Fill the derived fields: direction from Earth's longitude at launch, the drag γ that brings
 * the front to Earth at the observed arrival (or the speed, if even no drag is too slow), the
 * geoeffectiveness that makes the modelled peak Kp the observed one.
 */
void Calibrate(Cme& cme);

struct CmeCatalog {
    std::vector<Cme> events; // sorted by launchJd
};

/** Read resource/spaceweather/cmes.json's {"events":[…]}; skips (and reports) bad rows. */
bool ParseCmeCatalog(const Json::Value& document, CmeCatalog& out, std::string& error);
bool LoadCmeCatalog(const std::string& path, CmeCatalog& out, std::string& error);

/** "2003-10-29T06:11Z", "2003-10-29T06:11:00Z", "…:00.000Z" or "2003-10-29" (UTC) → JD; 0 if malformed. */
double ParseIsoUtc(const std::string& text);

// --- Magnetospheres -------------------------------------------------------------------------

struct Magnetosphere {
    OrbitLayout::Body body;
    const char* id;
    const char* name;
    double surfaceFieldTesla; // dipole field at the magnetic equator's surface
    double inflation;         // StandoffRadii's magnetodisc term
    OvalModel oval;
};

/** Mercury, Earth, Jupiter, Saturn, Uranus, Neptune: the bodies with a dipole field. */
const std::vector<Magnetosphere>& Magnetospheres();
const Magnetosphere* FindMagnetosphere(OrbitLayout::Body body);

/** Conditions at one magnetosphere. */
struct BodyState {
    const Magnetosphere* magnetosphere = nullptr;
    double rAu = 0.0;
    Wind wind;
    double standoffRadii = 0.0;
    double quietStandoffRadii = 0.0;
    double flaringExponent = 0.58;
    double coupling = 1.0;
    double kp = kQuietKp; // Kp at Earth; the same scale (0–9) for the others
    double kpUnclamped = kQuietKp;
    Oval oval;
};

/** Conditions at body `m` at heliocentric ecliptic (lon, lat, r) with `cmes` active. */
BodyState EvaluateBody(const Magnetosphere& m, double lonRad, double latRad, double rAu, double julianDate,
                       double cyclePhase, const std::vector<const Cme*>& cmes, double kpOverride);

// --- The state Application owns -------------------------------------------------------------

constexpr std::size_t kMaxActiveCmes = 4;

/**
 * The CME list, the Kp override and anything launched by hand; Evaluate() derives the frame's
 * cycle, active CMEs and per-magnetosphere conditions from the date.
 */
class State {
public:
    /** The active star's cycle (render.star.activity.cycle); periodYears 0 for none. */
    void SetCycle(const CycleParams& cycle) { _cycleParams = cycle; }
    const CycleParams& CycleParameters() const { return _cycleParams; }
    void SetCatalog(CmeCatalog catalog);
    const CmeCatalog& Catalog() const { return _catalog; }

    /** < 0 (default): Kp from the wind. 0–9: every oval held at that Kp. */
    void SetKpOverride(double kp) { _kpOverride = kp < 0.0 ? -1.0 : (kp > 9.0 ? 9.0 : kp); }
    double KpOverride() const { return _kpOverride; }

    /** An Earth-directed CME leaving the Sun at `julianDate` (the settings panel's button). */
    const Cme& LaunchCme(double julianDate, double speedKmS = 1800.0, double halfAngleDeg = 50.0);
    /** Drop CMEs launched by hand (a star-system jump or a reset). */
    void ClearLaunched() { _launched.clear(); }

    /** The catalog event whose launch or observed arrival is within `windowDays` of `jd`; -1 none. */
    int FindEventNear(double julianDate, double windowDays = 3.0) const;
    /** The first event arriving at Earth after `jd` with an observed or modelled Kp ≥ `minKp`; -1 none. */
    int NextStorm(double julianDate, double minKp = 5.0) const;
    /** The last event that arrived at Earth before `jd` with Kp ≥ `minKp`; -1 none. */
    int PreviousStorm(double julianDate, double minKp = 5.0) const;

    /**
     * Derive this frame's state at `julianDate`. `positions` gives each Magnetospheres() body's
     * heliocentric ecliptic (lon, lat, r AU) in the same order; Application reads them from the
     * ephemeris the scene is placed with.
     */
    void Evaluate(double julianDate, const std::vector<glm::dvec3>& positions);

    double JulianDate() const { return _jd; }
    const CycleState& Cycle() const { return _cycle; }
    /** At most kMaxActiveCmes CMEs whose fronts are inside the system now, newest first. */
    const std::vector<const Cme*>& ActiveCmes() const { return _active; }
    const std::vector<BodyState>& Bodies() const { return _bodies; }
    const BodyState* Body(OrbitLayout::Body body) const;
    /** Earth's Kp this frame (the override if set). */
    double Kp() const;

    /**
     * GetSpaceWeatherJson's payload. `extraFields` (`"enabled":true,…`, no braces) is what only
     * Application and the renderer know.
     */
    std::string ToJson(const std::string& extraFields) const;

private:
    CycleParams _cycleParams = SolarCycle();
    CmeCatalog _catalog;
    std::deque<Cme> _launched; // a deque: ActiveCmes() points into it across LaunchCme calls
    double _kpOverride = -1.0;
    double _jd = 0.0;
    CycleState _cycle;
    std::vector<const Cme*> _active;
    std::vector<BodyState> _bodies;
};

} // namespace SpaceWeather

#endif // SOLARSYSTEM_ASTRO_SPACEWEATHER_H
