#ifndef SOLARSYSTEM_IAPPMODULE_H
#define SOLARSYSTEM_IAPPMODULE_H
#include "ApplicationTypes.h"
#include "FrameContext.h"
#include "SimState.h"
#include "core/FrameProfile.h"
#include <array>
#include <memory>
#include <vector>

class Star;
struct ObserveState;

/**
 * What Application lets its modules read (docs/ARCHITECTURE.md §7.1): references into its own
 * state, valid for its lifetime. A module never writes through it; what it produces goes into
 * the frame's SceneSnapshot (IAppModule::Contribute) or out through its own API.
 */
struct ModuleContext {
    const std::vector<RenderableSceneComponent>& components;
    const std::shared_ptr<Star>& sun;
    const std::array<CameraView, 2>& views; // this frame's, from BuildFrameSnapshot
    const int& viewCount;                   // views drawn this frame (0: an XR frame without its eyes yet)
    const ObserveState& observe;
    const double& deltaSeconds; // this frame's step (a capture's fixed one)
};

/**
 * A subsystem with per-frame state of its own (terrain, space weather, missions). Application
 * owns the modules and, once a frame, after the scene is placed for the frame's date and its
 * views are known, calls Update and then Contribute on each in its list, billing the time to
 * Stage(). The renderer reads only what Contribute put in the snapshot.
 */
class IAppModule {
public:
    IAppModule() = default;
    virtual ~IAppModule() = default;
    IAppModule(const IAppModule&) = delete;
    IAppModule& operator=(const IAppModule&) = delete;

    virtual const char* Name() const = 0;
    /** The FrameProfile stage this module's CPU time is billed to. */
    virtual FrameProfile::Stage Stage() const { return FrameProfile::Stage::Update; }
    /** This frame's work: the scene is placed and SceneSnapshot's views are filled. */
    virtual void Update(SimState& sim) = 0;
    /** Hand the passes what they read of this module. */
    virtual void Contribute(SceneSnapshot& scene) = 0;
};

#endif // SOLARSYSTEM_IAPPMODULE_H
