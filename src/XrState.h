#ifndef SOLARSYSTEM_XRSTATE_H
#define SOLARSYSTEM_XRSTATE_H

#include <glm/glm.hpp>

/** Shared WebXR stereo state (Emscripten / WebGL 2 only). */
struct XrEyeState {
    glm::mat4 view{1.0f};
    glm::mat4 projection{1.0f};
    int viewportX = 0;
    int viewportY = 0;
    int viewportWidth = 0;
    int viewportHeight = 0;
};

struct XrFrameState {
    bool active = false;
    /**
     * Emscripten GL name for the XRWebGLLayer's opaque framebuffer, registered by
     * web/src/webxr.ts and 0 outside a session. This — not FBO 0 — is the default draw
     * target while a session runs, so every pass that renders off-screen must rebind
     * *this* on the way out or the eye images are lost.
     */
    unsigned int baseLayerFramebuffer = 0;
    int eyeCount = 0;
    int currentEye = 0;
    XrEyeState eyes[2]{};
    /** Scratch: [0..15]=view, [16..31]=proj for CommitXrEyeMatrices. */
    float matrixScratch[32]{};
};

#endif // SOLARSYSTEM_XRSTATE_H
