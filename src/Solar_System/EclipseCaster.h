#ifndef SOLARSYSTEM_ECLIPSECASTER_H
#define SOLARSYSTEM_ECLIPSECASTER_H

#include <glm/glm.hpp>

/**
 * Which moon, if any, is currently dropping a shadow on its primary.
 *
 * Split out of Application::ConfigureEclipseUmbra so the geometry is testable without a GL
 * context: the rest of that function is uniform uploads. Everything here is in scene units
 * and scene space, the same space the lighting shader's vFragPos and lightPos live in.
 */
namespace EclipseCaster {

/**
 * How far a moon's shadow axis passes from the planet's centre, in scene units.
 *
 * The axis is the line from the moon towards the star. Returns a negative value when the
 * moon is on the far side of the planet from the star, where its shadow falls into space
 * and can never touch the surface — callers treat any negative result as "no shadow".
 */
inline float ShadowAxisMissDistance(const glm::vec3& planetCenter, const glm::vec3& starCenter,
                                    const glm::vec3& moonCenter) {
    const glm::vec3 toStar = starCenter - planetCenter;
    const float starDistance = glm::length(toStar);
    if (starDistance < 1.0e-3f) {
        return -1.0f;
    }
    const glm::vec3 starDirection = toStar / starDistance;

    const glm::vec3 relative = moonCenter - planetCenter;
    const float along = glm::dot(relative, starDirection);
    if (along <= 0.0f) {
        return -1.0f;
    }
    return glm::length(relative - starDirection * along);
}

/**
 * True when that moon's shadow can reach the planet's disc. The threshold is the sum of the
 * two radii: a grazing shadow still darkens the limb, and the shader's own umbra/penumbra
 * falloff decides how much.
 */
inline bool CastsShadowOnPlanet(const glm::vec3& planetCenter, const glm::vec3& starCenter,
                                const glm::vec3& moonCenter, float planetRadius,
                                float moonRadius) {
    const float miss = ShadowAxisMissDistance(planetCenter, starCenter, moonCenter);
    return miss >= 0.0f && miss < planetRadius + moonRadius;
}

} // namespace EclipseCaster

#endif // SOLARSYSTEM_ECLIPSECASTER_H
