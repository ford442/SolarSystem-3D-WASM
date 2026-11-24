#ifndef SOLARSYSTEM_CAMERACONTROLLER_H
#define SOLARSYSTEM_CAMERACONTROLLER_H
#include "IAppModule.h"
#include "core/CameraNavigation.h"
#include <glm/glm.hpp>
#include <memory>
#include <vector>

class Camera;
class MissionController;
class SpaceObject;
class TerrainController;

/**
 * The fly camera's per-frame navigation (docs/ARCHITECTURE.md §11.1): it co-moves with the body
 * it is hovering near, flies at a speed set by its altitude, never goes below a surface, and
 * sets the clip planes and log-depth curve for that altitude. The math is GL-free in
 * core/CameraNavigation.h; this is where the scene's bodies are fed to it.
 *
 * Not an IAppModule: its two steps bracket the input (CarryWithAnchor before it, Constrain
 * after), well before the scene is placed for the frame and the modules run.
 */
class CameraController {
public:
    /** Where a body's drawn surface is along a direction from its centre. */
    struct SurfacePoint {
        double radius;     // centre to surface, scene units
        glm::dvec3 normal; // outward unit normal there, world frame
    };
    /** Where the camera sits relative to a body, in that body's radii (scale toggles keep it). */
    struct BodyFrame {
        std::shared_ptr<const SpaceObject> body;
        glm::dvec3 offsetInRadii{0.0};
    };

    /** Where the chase camera sits: `distance` behind `target` along `forward`, a little above. */
    struct ChaseTarget {
        glm::dvec3 target{0.0};            // heliocentric scene position of what is chased
        glm::dvec3 forward{1.0, 0.0, 0.0}; // its unit direction of motion
        double distance = 10.0;            // scene units
    };

    CameraController(Camera& camera, const ModuleContext& app, const TerrainController& terrain,
                     const MissionController& missions);

    /**
     * Chase mode (the flight planner's craft): pose the camera behind and above the target,
     * looking at it. Called once a frame before input while chasing; like mission follow it is
     * authored motion, so the camera drops its anchor body and does not co-rotate.
     */
    void Chase(const ChaseTarget& chase);
    void StopChase() { _chasing = false; }
    bool Chasing() const { return _chasing; }

    /** Before input: move the camera with its anchor body; set the fly speed for its altitude. */
    void CarryWithAnchor();
    /** After input: keep the camera above the surface, pick its anchor, set the clip planes. */
    void Constrain();
    /** Ride along with `body` (null: none); `pinned` holds it until a focus flight lands. */
    void SetAnchor(std::shared_ptr<const SpaceObject> body, bool pinned);
    /** The nearest body in its own radii, and the camera's offset from it in those radii. */
    BodyFrame CaptureBodyFrame() const;
    /** Put the camera back at `frame`'s offset from its body (after a scale change resized it). */
    void RestoreBodyFrame(const BodyFrame& frame);
    /** Every body the camera can fly near or collide with: the Sun, planets and moons. */
    std::vector<std::shared_ptr<const SpaceObject>> NavigationBodies() const;
    /**
     * The drawn surface of `object` along a world direction: the terrain tile, else the mesh
     * facet the floor measures.
     */
    SurfacePoint SurfaceOf(const SpaceObject& object, const glm::dvec3& direction) const;
    /** C of the log-depth curve this frame (CameraNavigation::ChooseClipPlanes). */
    float LogDepthC() const { return _logDepthC; }

private:
    struct Anchor {
        std::shared_ptr<const SpaceObject> body;
        CameraNavigation::Body last; // the body's pose when last carried (centre and drawn 3×3)
        bool pinned = false;         // a focus flight to this body: hold it until the flight lands
        bool coRotating = false;     // hovering: the camera turns with the body, not only follows it
    };

    Camera& _camera;
    const ModuleContext& _app;
    const TerrainController& _terrain;
    const MissionController& _missions;
    Anchor _anchor;
    float _logDepthC = 1.0f;
    bool _chasing = false;
};

#endif // SOLARSYSTEM_CAMERACONTROLLER_H
