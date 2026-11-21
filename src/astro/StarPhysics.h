#ifndef SOLARSYSTEM_ASTRO_STARPHYSICS_H
#define SOLARSYSTEM_ASTRO_STARPHYSICS_H

#include <glm/common.hpp>
#include <glm/vec3.hpp>
#include <algorithm>
#include <cmath>

/**
 * What a star's catalog block (render.star: effective temperature, radius, luminosity) implies
 * for the scene, GL-free so tests/test_star_physics.cpp can pin it.
 *
 * Every colour here is *relative to the Sun*: the engine's lighting, corona and flare were
 * tuned under a G2V, so a star's tint is its blackbody colour divided by the Sun's. At the
 * Sun's temperature that ratio is exactly (1, 1, 1), which is what keeps Sol's frames
 * byte-identical while a 2 566 K TRAPPIST-1 lights its planets red.
 */
namespace StarPhysics {

/** IAU 2015 Resolution B3 nominal solar effective temperature, the reference of every tint. */
constexpr double kSunEffectiveTemperatureK = 5772.0;
/** IAU 2015 nominal solar radius in km. */
constexpr double kSunRadiusKm = 695700.0;
constexpr double kKmPerAu = 149597870.7;

/**
 * The Planckian locus at `kelvin` in CIE 1931 xy, from Krystek's (1985) rational fit in CIE
 * 1960 UCS (u, v): better than 1e-4 in (u, v) over 1 000–15 000 K. Clamped to that range.
 */
inline void PlanckianXy(double kelvin, double& x, double& y) {
    const double t = std::clamp(kelvin, 1000.0, 15000.0);
    const double t2 = t * t;
    const double u =
        (0.860117757 + 1.54118254e-4 * t + 1.28641212e-7 * t2) / (1.0 + 8.42420235e-4 * t + 7.08145163e-7 * t2);
    const double v =
        (0.317398726 + 4.22806245e-5 * t + 4.20481691e-8 * t2) / (1.0 - 2.89741816e-5 * t + 1.61456053e-7 * t2);
    const double d = 2.0 * u - 8.0 * v + 4.0;
    x = 3.0 * u / d;
    y = 2.0 * v / d;
}

/**
 * Blackbody colour at `kelvin` in linear sRGB (Rec. 709 primaries, D65 white), at luminance
 * Y = 1. Negative channels (outside the sRGB gamut, which cool stars are in blue) clamp to 0.
 */
inline glm::dvec3 BlackbodyLinearRgb(double kelvin) {
    double x = 0.0;
    double y = 0.0;
    PlanckianXy(kelvin, x, y);
    const double X = x / y;
    const double Y = 1.0;
    const double Z = (1.0 - x - y) / y;
    const glm::dvec3 rgb(3.2404542 * X - 1.5371385 * Y - 0.4985314 * Z, -0.9692660 * X + 1.8760108 * Y + 0.0415560 * Z,
                         0.0556434 * X - 0.2040259 * Y + 1.0572252 * Z);
    return glm::max(rgb, glm::dvec3(0.0));
}

/**
 * The light a star casts, relative to the Sun's: its blackbody colour over the Sun's, scaled
 * so the brightest channel is 1 (a tint, not a brightness). (1, 1, 1) at the Sun's
 * temperature — exactly, since it is x / x — so multiplying lighting by it leaves Sol alone.
 */
inline glm::vec3 RelativeTint(double kelvin) {
    const glm::dvec3 sun = BlackbodyLinearRgb(kSunEffectiveTemperatureK);
    const glm::dvec3 star = BlackbodyLinearRgb(kelvin);
    const glm::dvec3 ratio(star.x / sun.x, star.y / sun.y, star.z / sun.z);
    const double peak = std::max({ratio.x, ratio.y, ratio.z});
    return peak > 0.0 ? glm::vec3(ratio / peak) : glm::vec3(1.0f);
}

/** Bolometric luminosity in L☉ from radius (R☉) and temperature: L = R² (T / T☉)⁴. */
inline double LuminosityFromRadius(double radiusSolar, double kelvin) {
    const double t = kelvin / kSunEffectiveTemperatureK;
    return radiusSolar * radiusSolar * t * t * t * t;
}

/** Irradiance at `au` from a star of `luminositySolar`, in Earth units (S⊕ = 1361 W/m²). */
inline double IrradianceEarthUnits(double luminositySolar, double au) {
    return au > 0.0 ? luminositySolar / (au * au) : 0.0;
}

/**
 * Zero-albedo equilibrium temperature in K at `au`: 278.6 K · L^¼ / √a, Earth's 278.6 K being
 * T☉ √(R☉ / 2 AU). Full heat redistribution; the archive's pl_eqt uses the same idea.
 */
inline double EquilibriumTemperatureK(double luminositySolar, double au) {
    if (au <= 0.0 || luminositySolar <= 0.0) {
        return 0.0;
    }
    return 278.6 * std::pow(luminositySolar, 0.25) / std::sqrt(au);
}

/**
 * The habitable zone as a simple flux window: from 1.1 S⊕ (runaway greenhouse, roughly
 * Kopparapu et al. 2013's inner edge) out to 0.53 S⊕ (maximum greenhouse), in AU.
 */
struct HabitableZone {
    double innerAu;
    double outerAu;
};

inline HabitableZone HabitableZoneAu(double luminositySolar) {
    const double l = std::max(luminositySolar, 0.0);
    return {std::sqrt(l / 1.1), std::sqrt(l / 0.53)};
}

/** The insolation at the middle of HabitableZoneAu's window, in log: √(1.1 × 0.53) S⊕. */
inline double HabitableZoneCentreFlux() {
    return std::sqrt(1.1 * 0.53);
}

/**
 * How far a planet at `au` is from the middle of the habitable zone, as |ln(S / S_c)|: 0 at
 * the centre, the same for twice and half its flux. Picks a system's "Earth-like" landing
 * planet: TRAPPIST-1 e (0.65 S⊕) over d (1.12 S⊕, just inside the runaway-greenhouse edge).
 */
inline double HabitabilityDistance(double luminositySolar, double au) {
    const double s = IrradianceEarthUnits(luminositySolar, au);
    return s > 0.0 ? std::fabs(std::log(s / HabitableZoneCentreFlux())) : 1.0e9;
}

/** Angular radius in radians of a star of `radiusKm` seen from `distanceKm` (exact, not small-angle). */
inline double AngularRadiusRad(double radiusKm, double distanceKm) {
    if (distanceKm <= radiusKm) {
        return 1.5707963267948966;
    }
    return std::asin(radiusKm / distanceKm);
}

} // namespace StarPhysics

#endif // SOLARSYSTEM_ASTRO_STARPHYSICS_H
