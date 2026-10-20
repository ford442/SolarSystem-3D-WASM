#ifndef SOLARSYSTEM_SCENESCALE_H
#define SOLARSYSTEM_SCENESCALE_H

#include "BodyCatalog.generated.h"
#include "OrbitLayout.h"

/**
 * Lengths of the true-scale world in scene units, per scale mode (docs/ARCHITECTURE.md §11.1).
 *
 * The world is true scale: every body has its real radius (Entry::radiusKm) and every moon
 * sits at its real distance from its parent (Ephemeris::SatellitePosition). Realistic scale
 * shows that world unchanged, at kKmPerSceneUnit kilometres per scene unit (1 AU =
 * OrbitLayout::kAuToSceneUnits). Compressed scale is a per-body radial remap of the same
 * world, applied where bodies are placed: planets on their art orbit radii (OrbitLayout),
 * moons on their catalog sceneOrbitRadius, bodies at their art radius (2 × earthRadiusScale,
 * sphere.obj's radius being 2). Only lengths change, never directions, so toggling the scale
 * re-places everything on the next frame without a reload.
 */
namespace SceneScale {

constexpr double kKmPerAu = 149597870.7;
/** Kilometres per scene unit on realistic scale (about 78 736). */
constexpr double kKmPerSceneUnit = kKmPerAu / static_cast<double>(OrbitLayout::kAuToSceneUnits);
/** sphere.obj's radius: catalog bodies scale it by earthRadiusScale on compressed scale. */
constexpr float kMeshRadius = 2.0f;
constexpr double kSunRadiusKm = 695700.0;
/** The Sun's drawn radius on compressed scale (sphere.obj at Sun::TakeStarSystemCenter's 0.5). */
constexpr float kSunArtRadius = 1.0f;

inline bool IsTrueScale() {
    return OrbitLayout::GetScaleMode() == OrbitLayout::ScaleMode::Realistic;
}

/** Kilometres → scene units at realistic (true) scale. */
inline double KmToScene(double km) {
    return km / kKmPerSceneUnit;
}

/**
 * Drawn radius over art radius: 1 on compressed scale, true radius / art radius on realistic.
 * The art mesh scale (earthRadiusScale) times this is the body's scale, and every length
 * tied to the body — shells, rings, focus and LOD distances — scales with it, so compressed
 * scale keeps exactly the numbers it always had.
 */
inline float BodySizeFactor(const BodyCatalog::Entry& entry) {
    if (!IsTrueScale() || entry.earthRadiusScale <= 0.0f || entry.radiusKm <= 0.0f) {
        return 1.0f;
    }
    return static_cast<float>(KmToScene(entry.radiusKm) / (kMeshRadius * entry.earthRadiusScale));
}

/** BodySizeFactor for the Sun, which has no catalog row. */
inline float SunSizeFactor() {
    return IsTrueScale() ? static_cast<float>(KmToScene(kSunRadiusKm) / kSunArtRadius) : 1.0f;
}

/** Drawn (equatorial) radius in scene units. */
inline float BodyRadius(const BodyCatalog::Entry& entry) {
    return kMeshRadius * entry.earthRadiusScale * BodySizeFactor(entry);
}

inline float SunRadius() {
    return kSunArtRadius * SunSizeFactor();
}

/**
 * Scene units per AU of a moon's parent-relative offset. Compressed keeps the orbit's shape
 * and tilt but puts its semi-major axis on sceneOrbitRadius; realistic is the true distance.
 * 0 when the row has no semi-major axis to scale by.
 */
inline double SatelliteSceneUnitsPerAu(const BodyCatalog::Entry& entry) {
    if (entry.keplerian.aKm <= 0.0f || entry.sceneOrbitRadius <= 0.0f) {
        return 0.0;
    }
    if (IsTrueScale()) {
        return static_cast<double>(OrbitLayout::kAuToSceneUnits);
    }
    return static_cast<double>(entry.sceneOrbitRadius) / (static_cast<double>(entry.keplerian.aKm) / kKmPerAu);
}

/** Radius of a moon's circular stand-in orbit (no ephemeris solution), in scene units. */
inline double SatelliteCircularRadius(const BodyCatalog::Entry& entry) {
    if (IsTrueScale() && entry.keplerian.aKm > 0.0f) {
        return KmToScene(entry.keplerian.aKm);
    }
    return entry.sceneOrbitRadius;
}

} // namespace SceneScale

#endif // SOLARSYSTEM_SCENESCALE_H
