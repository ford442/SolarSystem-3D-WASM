#ifndef SOLARSYSTEM_BODY_SHAPE_H
#define SOLARSYSTEM_BODY_SHAPE_H

#include "BodyCatalog.generated.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

/**
 * A catalog body's figure: its pole orientation in the scene and its flattening
 * (render.oblateness, 1 - polar/equatorial). GL-free, shared by the meshes that have to
 * agree on it — CatalogBody (the surface), CatalogClouds (the cloud shell) and Atmosphere
 * (the air above them) — and by the atmosphere shaders, which trace their view ray in the
 * frame where the oblate figure is a sphere of the equatorial radius.
 *
 * The mesh radius is the equatorial one (what earthRadiusScale and groundRadiusKm mean);
 * only the polar axis shrinks. The tilt matches the Rotate(Z, axialTilt) then
 * Rotate(X, artTiltX) order the meshes and the ring use. See docs/ARCHITECTURE.md §9.2.
 */
namespace BodyShape {

/** Body frame → scene: axial tilt about Z, then the art tilt about X. Spin excluded. */
inline glm::mat3 PoleFrame(const BodyCatalog::Entry& entry) {
    glm::mat4 frame = glm::rotate(glm::mat4(1.0f), glm::radians(entry.axialTiltDegrees), glm::vec3(0.0f, 0.0f, 1.0f));
    if (entry.artTiltXDegrees != 0.0f) {
        frame = glm::rotate(frame, glm::radians(entry.artTiltXDegrees), glm::vec3(1.0f, 0.0f, 0.0f));
    }
    return glm::mat3(frame);
}

inline bool IsOblate(const BodyCatalog::Entry& entry) {
    return entry.oblateness > 0.0f;
}

/** Scale applied in the body frame, after the tilt and before the spin (which commutes). */
inline glm::vec3 PolarScale(const BodyCatalog::Entry& entry) {
    return {1.0f, 1.0f - entry.oblateness, 1.0f};
}

/** Scene-space offset from the centre → the frame where the figure is a sphere. */
inline glm::mat3 BodyToSphere(const BodyCatalog::Entry& entry) {
    const glm::mat3 frame = PoleFrame(entry);
    const glm::mat3 stretch(glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f / (1.0f - entry.oblateness), 0.0f),
                            glm::vec3(0.0f, 0.0f, 1.0f));
    return frame * stretch * glm::transpose(frame);
}

/** Inverse of BodyToSphere. */
inline glm::mat3 SphereToBody(const BodyCatalog::Entry& entry) {
    const glm::mat3 frame = PoleFrame(entry);
    const glm::vec3 scale = PolarScale(entry);
    const glm::mat3 squash(glm::vec3(scale.x, 0.0f, 0.0f), glm::vec3(0.0f, scale.y, 0.0f),
                           glm::vec3(0.0f, 0.0f, scale.z));
    return frame * squash * glm::transpose(frame);
}

} // namespace BodyShape

#endif // SOLARSYSTEM_BODY_SHAPE_H
