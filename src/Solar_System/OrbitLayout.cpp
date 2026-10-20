#include "OrbitLayout.h"

#include "../Auxiliary_Modules/Ephemeris.h"
#include "../Auxiliary_Modules/Observer.h"
#include "../Auxiliary_Modules/SkyEvents.h"
#include "BodyCatalog.generated.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
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
    float primeMeridianDegJ2000; // IAU W0, the spin epoch (Earth solves its own, see below)
};

constexpr float kEarthYearDays = 365.25f;

/**
 * Geographic longitude at the centre (u = 0.5) of the Earth diffuse map. The reference is
 * the map convention itself: NASA Blue Marble / Solar System Scope Earth maps are centred
 * on Greenwich, and the Blender sphere.obj puts u = 0.5 on model +X (texture longitude =
 * -atan2(z, x) in model space). tests/test_orbit_layout.cpp locks the result against
 * almanac sub-solar longitudes, so changing this without a recentred texture fails there.
 */
constexpr double kEarthTextureCentreLonDeg = 0.0;

// NASA fact-sheet averages for periods/inclination/sidereal day.
// Sidereal rotation days: Venus and Uranus are retrograde (negative).
// Generated from resource/planets.catalog.json — see OrbitLayoutBodies.generated.inc header
// comment. Run `node scripts/generate-planet-metadata.mjs` after editing the catalog.
constexpr BodyData kBodies[] = {
#include "OrbitLayoutBodies.generated.inc"
};

static_assert(sizeof(kBodies) / sizeof(kBodies[0]) == static_cast<std::size_t>(kBodyCount),
              "Generated body rows must cover every OrbitLayout::Body value — "
              "rerun scripts/generate-planet-metadata.mjs after editing the catalog.");

ScaleMode g_scaleMode = ScaleMode::Compressed;
double g_julianDate = Ephemeris::kJ2000;
// Kept in double: rounding these to float is what put a ~0.004-unit grid under Neptune.
double g_lonRad[kBodyCount] = {};
double g_latRad[kBodyCount] = {};
double g_rAu[kBodyCount] = {};
glm::dvec3 g_renderOrigin(0.0);

int bodyIndex(Body body) {
    const int idx = static_cast<int>(body);
    return std::clamp(idx, 0, kBodyCount - 1);
}

void refreshEphemerisCache() {
    for (int i = 0; i < kBodyCount; ++i) {
        const Ephemeris::HelioLB pos = Ephemeris::Position(i, g_julianDate);
        g_lonRad[i] = pos.lonRad;
        g_latRad[i] = pos.latRad;
        g_rAu[i] = pos.rAu;
    }
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

double wrapDegrees(double deg) {
    const double w = std::fmod(deg, 360.0);
    return w < 0.0 ? w + 360.0 : w;
}

/**
 * Earth's spin about model +Y (CatalogBody::AdjustToParent's last Rotate) that puts the
 * real sub-solar point under the scene's Sun on the texture at g_julianDate.
 *
 * GMST plus a constant cannot do this: scene axes are (ecliptic x, z, y), a reflection of
 * the ecliptic, and the drawn pole is art (Rz(axialTilt)), so the Sun's azimuth about the
 * drawn pole runs backwards over the year — with the old GMST spin the texture's sub-solar
 * longitude was off by up to ~180 deg depending on the season. Instead take the Sun's true
 * Earth-fixed longitude (Observer: GMST, precession, J2000 ecliptic) and solve for the spin
 * that turns that longitude of the map toward the scene's Sun.
 *
 * Only longitude is calibrated: the sub-solar latitude is whatever the art pole gives.
 */
double earthSpinDegrees() {
    // Horizon frame at lat 0 / lon 0: X = east (body lon 90E), Y = up (lon 0), Z = -north.
    double sunEcl[3] = {0.0, 0.0, 0.0};
    SkyEvents::GeocentricEclipticKm(0, g_julianDate, sunEcl);
    double sunHorizon[3];
    Observer::HorizonFromEclipticJ2000(g_julianDate, Observer::Site{}).Apply(sunEcl, sunHorizon);
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
    const glm::dvec3 s = glm::transpose(tilt) * -GetHeliocentricScenePosition(Body::Earth);

    // The map longitude of a model direction is centre - atan2(z, x), and undoing Ry(spin)
    // raises the Sun's atan2(z, x) by `spin`, so the longitude facing the Sun is
    // centre - (azimuth + spin). Set that to the true one.
    const double azimuthDeg = glm::degrees(std::atan2(s.z, s.x));
    return wrapDegrees(kEarthTextureCentreLonDeg - trueLonDeg - azimuthDeg);
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
    g_scaleMode = ScaleMode::Compressed;
    g_renderOrigin = glm::dvec3(0.0);
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
    refreshEphemerisCache();
}

glm::vec3 GetCompressedOffset(Body body) {
    return kBodies[bodyIndex(body)].compressedOffset;
}

float GetAuDistance(Body body) {
    return kBodies[bodyIndex(body)].auDistance;
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
    struct Key {
        double au;
        double scene;
    };
    const auto key = [](Body body) {
        return Key{GetAuDistance(body), glm::length(GetCompressedOffset(body))};
    };
    const Key keys[] = {
        key(Body::Mercury), key(Body::Venus), key(Body::Earth),
        key(Body::Mars), key(Body::Jupiter), key(Body::Saturn),
        key(Body::Uranus), key(Body::Neptune), key(Body::Pluto),
    };
    constexpr int keyCount = static_cast<int>(sizeof(keys) / sizeof(keys[0]));

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
    return kBodies[bodyIndex(body)].orbitalPeriodDays;
}

float GetSiderealRotationDays(Body body) {
    return kBodies[bodyIndex(body)].siderealRotationDays;
}

float GetOrbitRadius(Body body) {
    return static_cast<float>(orbitRadius(body));
}

bool UsesTrueRadius(Body body) {
    return static_cast<int>(body) >= static_cast<int>(Body::Eris);
}

float GetHeliocentricRadiusAu(Body body) {
    return static_cast<float>(g_rAu[bodyIndex(body)]);
}

float GetSceneDistance(Body body) {
    return GetOrbitRadius(body);
}

float GetAxialSpinDegrees(Body body) {
    // Every spin is a function of the date, never an accumulator, so scrubbing the date and
    // coming back reproduces it exactly (CLAUDE.md, "Poses are functions of the date").
    //
    // Earth is calibrated: the map longitude under the Sun is the real sub-solar longitude
    // (earthSpinDegrees). Every other body turns from its IAU WGCCRE 2015 prime-meridian
    // angle at J2000 (catalog orbit.primeMeridianDegJ2000; 0 where the IAU gives none) at
    // the catalog sidereal rate. Their poles and textures are art, not IAU frames, so W0
    // only fixes a reproducible epoch; it does not put a named feature at a real longitude.
    if (body == Body::Earth) {
        return static_cast<float>(earthSpinDegrees());
    }
    const BodyData& row = kBodies[bodyIndex(body)];
    if (row.siderealRotationDays == 0.0f) {
        return row.primeMeridianDegJ2000;
    }
    const double days = g_julianDate - Ephemeris::kJ2000;
    return static_cast<float>(wrapDegrees(static_cast<double>(row.primeMeridianDegJ2000) +
                                          360.0 * days / static_cast<double>(row.siderealRotationDays)));
}

float GetInclinationDegrees(Body body) {
    return kBodies[bodyIndex(body)].inclinationDeg;
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

glm::dvec3 ChooseRenderOrigin(const glm::vec3& cameraRenderFrame) {
    if (g_scaleMode != ScaleMode::Realistic) {
        return glm::dvec3(0.0);
    }
    if (glm::length(cameraRenderFrame) <= kRenderOriginRebaseDistance) {
        return g_renderOrigin;
    }
    return ToHeliocentricScene(cameraRenderFrame);
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
    if (name == "Ceres") return Body::Ceres;
    if (name == "Vesta") return Body::Vesta;
    if (name == "Eris") return Body::Eris;
    if (name == "Haumea") return Body::Haumea;
    if (name == "Makemake") return Body::Makemake;
    if (name == "Halley") return Body::Halley;
    return Body::Sun;
}

} // namespace OrbitLayout
