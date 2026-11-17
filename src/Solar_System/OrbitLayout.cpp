#include "OrbitLayout.h"

#include "../astro/Ephemeris.h"
#include "../astro/Observer.h"
#include "../astro/SkyEvents.h"
#include "BodyCatalog.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <string>
#include <vector>

namespace OrbitLayout {
namespace {

/**
 * Geographic longitude at the centre (u = 0.5) of the Earth diffuse map: the Greenwich-
 * centred equirectangular convention of NASA Blue Marble and Solar System Scope maps. The
 * drawn sphere (the Blender sphere.obj; scripts/generate_sphere.py is only a stand-in) puts
 * u = 0.5 on model +X and u rising as atan2(z, x) falls, so a model direction's map
 * longitude is -atan2(z, x). tests/test_orbit_layout.cpp locks this constant against the
 * real sub-solar longitude all year, so changing it without recentring the texture fails.
 */
constexpr double kEarthTextureCentreLonDeg = 0.0;

// NASA fact-sheet averages for periods/inclination/sidereal day.
// Sidereal rotation days: Venus and Uranus are retrograde (negative).
// Generated from resource/systems/sol.catalog.json — see OrbitLayoutBodies.generated.inc header
// comment. Run `node scripts/generate-planet-metadata.mjs` after editing the catalog.
constexpr BodyRow kSolBodies[] = {
#include "OrbitLayoutBodies.generated.inc"
};

static_assert(sizeof(kSolBodies) / sizeof(kSolBodies[0]) == static_cast<std::size_t>(kBodyCount),
              "Generated body rows must cover every OrbitLayout::Body value — "
              "rerun scripts/generate-planet-metadata.mjs after editing the catalog.");

// The active system's table: Sol's compiled rows unless SetSystemRows chose another system's.
const BodyRow* g_bodies = kSolBodies;
int g_bodyCount = kBodyCount;
bool g_isSol = true;

ScaleMode g_scaleMode = ScaleMode::Compressed;
double g_julianDate = Ephemeris::kJ2000;
// Kept in double: rounding these to float is what put a ~0.004-unit grid under Neptune.
std::vector<double> g_lonRad(kBodyCount, 0.0);
std::vector<double> g_latRad(kBodyCount, 0.0);
std::vector<double> g_rAu(kBodyCount, 0.0);
glm::dvec3 g_renderOrigin(0.0);

/** The compressed-scale remap's keys (AuToSceneDistanceDouble), fixed per system. */
struct Key {
    double au;
    double scene;
};
std::vector<Key> g_compressedKeys;

int bodyIndex(Body body) {
    const int idx = static_cast<int>(body);
    return std::clamp(idx, 0, g_bodyCount - 1);
}

const BodyRow& row(Body body) {
    return g_bodies[bodyIndex(body)];
}

void refreshEphemerisCache() {
    for (int i = 0; i < g_bodyCount; ++i) {
        Ephemeris::HelioLB pos;
        if (g_isSol) {
            pos = Ephemeris::Position(i, g_julianDate);
        } else if (g_bodies[i].kepler.aAu > 0.0) {
            const KeplerElements& k = g_bodies[i].kepler;
            pos = Ephemeris::KeplerianPosition(k.aAu, k.e, k.iDeg, k.OmegaDeg, k.omegaDeg, k.M0Deg, k.nDegPerDay,
                                               g_julianDate);
        }
        g_lonRad[i] = pos.lonRad;
        g_latRad[i] = pos.latRad;
        g_rAu[i] = pos.rAu;
    }
}

/**
 * The compressed scale's piecewise-linear AU → scene map runs through the planets' art orbit
 * radii. Sol's keys are Mercury … Pluto, in that order, exactly as before; another system's are
 * its own focus bodies (not the star, not the eccentric true-radius ones), sorted by distance.
 */
void rebuildCompressedKeys() {
    g_compressedKeys.clear();
    const auto key = [](Body body) { return Key{GetAuDistance(body), glm::length(GetCompressedOffset(body))}; };
    if (g_isSol) {
        for (const Body body : {Body::Mercury, Body::Venus, Body::Earth, Body::Mars, Body::Jupiter, Body::Saturn,
                                Body::Uranus, Body::Neptune, Body::Pluto}) {
            g_compressedKeys.push_back(key(body));
        }
        return;
    }
    for (int i = 1; i < g_bodyCount; ++i) {
        if (g_bodies[i].auDistance > 0.0f && !g_bodies[i].trueRadius) {
            g_compressedKeys.push_back(key(static_cast<Body>(i)));
        }
    }
    std::sort(g_compressedKeys.begin(), g_compressedKeys.end(), [](const Key& a, const Key& b) { return a.au < b.au; });
}

double wrapDegrees(double deg) {
    const double wrapped = std::fmod(deg, 360.0);
    return wrapped < 0.0 ? wrapped + 360.0 : wrapped;
}

/**
 * Earth's spin about model +Y (CatalogBody::AdjustToParent's last Rotate) that puts the
 * real sub-solar longitude of `julianDate` under the scene's Sun on the drawn map.
 *
 * It is not GMST plus a constant, and cannot be. Scene axes are (ecliptic x, z, y), a
 * reflection, and the drawn pole is art (Rz(axialTilt)), so about the drawn pole the Sun
 * circles the year the wrong way round: a texture turned at the GMST rate with any fixed
 * offset is right at one instant and off by 2° a day from there (a full turn over a year).
 * The Sun's true Earth-fixed longitude comes from Observer (GMST, precession, J2000
 * ecliptic); the spin turns that longitude of the map toward the scene's Sun. The rate is
 * still the sidereal one, to within the Sun's own ~1°/day, and the epoch behaviour is
 * unchanged: a pure function of the date.
 *
 * Only longitude is calibrated. The sub-solar latitude is whatever the art pole gives.
 */
double earthSpinDegrees(double julianDate) {
    // Horizon frame at lat 0 / lon 0: X = east (body lon 90E), Y = up (lon 0), Z = -north.
    double sunEcl[3] = {0.0, 0.0, 0.0};
    SkyEvents::GeocentricEclipticKm(0, julianDate, sunEcl);
    double sunHorizon[3];
    Observer::HorizonFromEclipticJ2000(julianDate, Observer::Site{}).Apply(sunEcl, sunHorizon);
    const double trueLonDeg = glm::degrees(std::atan2(sunHorizon[0], sunHorizon[1]));

    // The scene Sun in the model frame before the spin: undo Rz(tilt) then Rx(artTiltX).
    // Uniform and polar scale leave the azimuth about +Y alone.
    glm::dmat3 tilt(1.0);
    if (const BodyCatalog::Entry* earth = BodyCatalog::FindById("earth")) {
        const double z = glm::radians(static_cast<double>(earth->axialTiltDegrees));
        const double x = glm::radians(static_cast<double>(earth->artTiltXDegrees));
        const glm::dmat3 rz(std::cos(z), std::sin(z), 0.0, -std::sin(z), std::cos(z), 0.0, 0.0, 0.0, 1.0);
        const glm::dmat3 rx(1.0, 0.0, 0.0, 0.0, std::cos(x), std::sin(x), 0.0, -std::sin(x), std::cos(x));
        tilt = rz * rx;
    }
    const Ephemeris::HelioLB earthPos = Ephemeris::Position(static_cast<int>(Body::Earth), julianDate);
    const double cosLat = std::cos(earthPos.latRad);
    const glm::dvec3 sunDirection = -glm::dvec3(std::cos(earthPos.lonRad) * cosLat, std::sin(earthPos.latRad),
                                                std::sin(earthPos.lonRad) * cosLat);
    const glm::dvec3 s = glm::transpose(tilt) * sunDirection;

    // Map longitude of a model direction is centre - atan2(z, x); undoing Ry(spin) raises
    // the Sun's atan2(z, x) by `spin`, so the longitude facing the Sun is
    // centre - (azimuth + spin). Set that to the true one.
    const double azimuthDeg = glm::degrees(std::atan2(s.z, s.x));
    return wrapDegrees(kEarthTextureCentreLonDeg - trueLonDeg - azimuthDeg);
}

/** GetOrbitRadius in double. */
double orbitRadius(Body body) {
    if (body == Body::Sun) {
        return 0.0;
    }
    if (UsesTrueRadius(body)) {
        return AuToSceneDistanceDouble(g_rAu[bodyIndex(body)]);
    }
    if (g_scaleMode == ScaleMode::Compressed) {
        return static_cast<double>(glm::length(GetCompressedOffset(body)));
    }
    return AuToSceneDistanceDouble(static_cast<double>(GetAuDistance(body)));
}

} // namespace

void SetSystemRows(const BodyRow* rows, int count) {
    if (rows && count > 0) {
        g_bodies = rows;
        g_bodyCount = count;
        g_isSol = false;
    } else {
        g_bodies = kSolBodies;
        g_bodyCount = kBodyCount;
        g_isSol = true;
    }
    g_lonRad.assign(static_cast<std::size_t>(g_bodyCount), 0.0);
    g_latRad.assign(static_cast<std::size_t>(g_bodyCount), 0.0);
    g_rAu.assign(static_cast<std::size_t>(g_bodyCount), 0.0);
    rebuildCompressedKeys();
    refreshEphemerisCache();
}

int BodyCount() {
    return g_bodyCount;
}

bool IsSolLayout() {
    return g_isSol;
}

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
    g_scaleMode = ScaleMode::Compressed;
    g_renderOrigin = glm::dvec3(0.0);
    g_julianDate = Ephemeris::kJ2000;
    SetSystemRows(nullptr, 0);
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
    refreshEphemerisCache();
}

glm::vec3 GetCompressedOffset(Body body) {
    return row(body).compressedOffset;
}

float GetAuDistance(Body body) {
    return row(body).auDistance;
}

double AuToSceneDistanceDouble(double au) {
    if (au <= 0.0) {
        return 0.0;
    }
    if (g_scaleMode == ScaleMode::Realistic) {
        return au * static_cast<double>(kAuToSceneUnits);
    }

    // Piecewise-linear remap through planet compressed orbit radii so the
    // main belt (and comets) sit between Mars and Jupiter in art scale.
    if (g_compressedKeys.empty()) {
        rebuildCompressedKeys();
    }
    if (g_compressedKeys.empty()) {
        return au * static_cast<double>(kAuToSceneUnits); // a system with no planet yet
    }
    const Key* keys = g_compressedKeys.data();
    const int keyCount = static_cast<int>(g_compressedKeys.size());

    if (au <= keys[0].au) {
        return keys[0].scene * (au / keys[0].au);
    }
    for (int i = 0; i < keyCount - 1; ++i) {
        if (au <= keys[i + 1].au) {
            const double t = (au - keys[i].au) / (keys[i + 1].au - keys[i].au);
            return keys[i].scene + t * (keys[i + 1].scene - keys[i].scene);
        }
    }
    const Key& last = keys[keyCount - 1];
    return last.scene * (au / last.au);
}

float AuToSceneDistance(float au) {
    return static_cast<float>(AuToSceneDistanceDouble(static_cast<double>(au)));
}

glm::vec3 HelioAuToScene(const glm::vec3& auSceneAxes) {
    const float r = glm::length(auSceneAxes);
    if (r < 1.0e-8f) {
        return glm::vec3(0.0f);
    }
    return auSceneAxes * (AuToSceneDistance(r) / r);
}

glm::dvec3 HelioAuToSceneDouble(const glm::dvec3& auSceneAxes) {
    const double r = glm::length(auSceneAxes);
    if (r < 1.0e-12) {
        return glm::dvec3(0.0);
    }
    return auSceneAxes * (AuToSceneDistanceDouble(r) / r);
}

float GetOrbitalPeriodDays(Body body) {
    return row(body).orbitalPeriodDays;
}

float GetSiderealRotationDays(Body body) {
    return row(body).siderealRotationDays;
}

float GetOrbitRadius(Body body) {
    return static_cast<float>(orbitRadius(body));
}

bool UsesTrueRadius(Body body) {
    if (!g_isSol) {
        return row(body).trueRadius;
    }
    return static_cast<int>(body) >= static_cast<int>(Body::Eris);
}

float GetHeliocentricRadiusAu(Body body) {
    return static_cast<float>(g_rAu[bodyIndex(body)]);
}

float GetSceneDistance(Body body) {
    return GetOrbitRadius(body);
}

double AxialSpinDegreesAt(Body body, double julianDate) {
    // Every spin is a function of the date, never an accumulator (CLAUDE.md, "Poses are
    // functions of the date"): a reload, a shared ?date= link, a timeline that starts at a
    // keyframe, or scrubbing away and back all land on the same longitude.
    //
    // Earth is solved so the drawn map shows the real sub-solar longitude (earthSpinDegrees).
    // Every other body turns from its IAU WGCCRE W0 at J2000 at its catalog Wdot, which is
    // negative for the retrograde rotators (Venus, Uranus) and needs no special case. Their
    // poles and textures are art, not IAU frames, so W0 fixes a reproducible epoch; it does
    // not by itself put a named feature at a real longitude.
    if (g_isSol && body == Body::Earth) {
        return earthSpinDegrees(julianDate);
    }
    const BodyRow& r = row(body);
    const double w = r.primeMeridianDegJ2000 + r.primeMeridianRateDegPerDay * (julianDate - Ephemeris::kJ2000);
    // The scene maps ecliptic y to +Z (GetHeliocentricScenePosition), a mirror: a prograde
    // orbit runs the negative way about +Y. Sol's spins were tuned in that frame and are kept
    // as they are; another system's are mirrored back, so a tidally locked planet (W at the
    // mean motion, W0 = λ0 − 180°) keeps its sub-stellar point at map longitude 0.
    return wrapDegrees(g_isSol ? w : -w);
}

float GetAxialSpinDegrees(Body body) {
    return static_cast<float>(AxialSpinDegreesAt(body, g_julianDate));
}

float GetInclinationDegrees(Body body) {
    return row(body).inclinationDeg;
}

glm::dvec3 GetHeliocentricScenePosition(Body body) {
    if (body == Body::Sun) {
        return glm::dvec3(0.0);
    }

    const int idx = bodyIndex(body);
    const double radius = orbitRadius(body);
    if (radius < 0.001) {
        return glm::dvec3(0.0);
    }

    const double lon = g_lonRad[idx];
    const double lat = g_latRad[idx];
    const double cosLat = std::cos(lat);
    // Scene Y-up: ecliptic x→X, ecliptic z→Y, ecliptic y→Z.
    return glm::dvec3(
        radius * std::cos(lon) * cosLat,
        radius * std::sin(lat),
        radius * std::sin(lon) * cosLat);
}

glm::vec3 GetOffset(Body body) {
    return glm::vec3(GetHeliocentricScenePosition(body));
}

glm::dvec3 GetRenderOrigin() {
    return g_renderOrigin;
}

void SetRenderOrigin(const glm::dvec3& origin) {
    g_renderOrigin = origin;
}

glm::vec3 ToRenderFrame(const glm::dvec3& heliocentricScene) {
    return glm::vec3(heliocentricScene - g_renderOrigin);
}

glm::dvec3 ToHeliocentricScene(const glm::vec3& renderFrame) {
    return glm::dvec3(renderFrame) + g_renderOrigin;
}

glm::vec3 GetRenderPosition(Body body) {
    return ToRenderFrame(GetHeliocentricScenePosition(body));
}

Body BodyFromName(const std::string& name) {
    if (!g_isSol) {
        // Another system: its catalog ids and display names (names are ASCII designations).
        for (const BodyCatalog::Entry& entry : BodyCatalog::Entries()) {
            if (entry.kind == BodyCatalog::Kind::Satellite) {
                continue;
            }
            const std::wstring wide(entry.displayNameEn);
            if (name == entry.id || name == std::string(wide.begin(), wide.end())) {
                return static_cast<Body>(entry.index);
            }
        }
        return Body::Sun;
    }
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
    if (name == "Eris") return Body::Eris;
    if (name == "Haumea") return Body::Haumea;
    if (name == "Makemake") return Body::Makemake;
    if (name == "Halley") return Body::Halley;
    return Body::Sun;
}

} // namespace OrbitLayout
