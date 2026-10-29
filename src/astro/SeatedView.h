#ifndef SOLARSYSTEM_SEATEDVIEW_H
#define SOLARSYSTEM_SEATEDVIEW_H

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

/**
 * Seated sky viewing in a headset. The head pose comes from WebXR's reference space (forward is
 * -Z, up is +Y, both fixed by the room, not by the sky); the sky is in the observer's horizon
 * frame (+X East, +Y Up, +Z -North). A single rotation about the vertical joins them: the
 * *heading* — the azimuth the viewer's reference-space "forward" currently faces — which a
 * snap turn changes. Nothing else of the camera applies: pitch and roll are the viewer's own
 * head, and translation is dropped because the sky is at infinity.
 */
namespace SeatedView {

/**
 * Horizon-frame → eye-view rotation: `eyeViewRotation` is the rotation part of the XR eye's
 * view matrix (reference space → eye), and `headingAzimuthDeg` the azimuth, from north through
 * east, that the reference space's forward (-Z) maps to. glm::rotate about +Y is
 * counter-clockwise seen from above, which is also how azimuth is measured away from north
 * toward east when looking down on the horizon — hence the plain +heading.
 */
inline glm::mat3 ViewRotation(const glm::mat3& eyeViewRotation, float headingAzimuthDeg) {
    const glm::mat3 heading = glm::mat3(glm::rotate(glm::mat4(1.0f), glm::radians(headingAzimuthDeg),
                                                    glm::vec3(0.0f, 1.0f, 0.0f)));
    return eyeViewRotation * heading;
}

} // namespace SeatedView

#endif // SOLARSYSTEM_SEATEDVIEW_H
