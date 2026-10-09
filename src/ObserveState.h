#ifndef SOLARSYSTEM_OBSERVESTATE_H
#define SOLARSYSTEM_OBSERVESTATE_H

#include "Auxiliary_Modules/Observer.h"
#include <glm/glm.hpp>

/**
 * Observe mode: the camera stands on Earth at `site` and the sky is drawn by ObserveSky from
 * Observer::ComputeSky. The simulation epoch stays the one OrbitLayout::GetJulianDate(), so
 * the date controls keep working; only how fast it advances changes (`timeRate`).
 *
 * Lives on Application (not SimState, which the unit tests link) — see Application.h.
 */
struct ObserveState {
    bool active = false;
    Observer::Site site{51.4769, 0.0, 0.0}; // Greenwich, the default when nothing else is chosen
    double timeRate = 1.0;                  // simulated seconds per wall second; 1 = real time

    // Explore-mode camera, restored when Observe is left.
    glm::vec3 savedPosition{0.0f};
    float savedYaw = -90.0f;
    float savedPitch = 0.0f;
    float savedZoom = 45.0f;
    float savedZoomMin = 1.0f;
    float savedZoomMax = 45.0f;
    bool savedMovementLocked = false;

    // Where Observe looks on entry, and the FOV range while it is active.
    static constexpr float kDefaultFovDeg = 60.0f;
    static constexpr float kMinFovDeg = 2.0f;
    static constexpr float kMaxFovDeg = 90.0f;
    static constexpr float kDefaultAzimuthDeg = 180.0f; // facing south from the northern hemisphere
    static constexpr float kDefaultElevationDeg = 25.0f;
    // Camera sits this far above Earth's centre so staged loading and LOD see "near Earth".
    static constexpr float kEarthSurfaceSceneUnits = 2.0f;
};

#endif // SOLARSYSTEM_OBSERVESTATE_H
