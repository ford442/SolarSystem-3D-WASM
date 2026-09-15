# WebXR immersive VR (WASM renderer)

Exploratory WebXR path for the C++/Emscripten WebGL 2 app. Companion Three.js notes at the end.

## User-facing behavior

- Settings panel shows **Enter VR** only when `navigator.xr.isSessionSupported('immersive-vr')` is true.
- Non-XR browsers never see the button; desktop 2-D path is unchanged.
- Entering VR:
  1. `gl.makeXRCompatible()` on the existing WebGL 2 context
  2. `XRWebGLLayer` as `baseLayer`
  3. Pauses the Emscripten main loop (`emscripten_pause_main_loop`)
  4. Drives frames with `session.requestAnimationFrame`
  5. Defaults quality to **medium** (or keeps **low** if already low)
- Controllers: left stick → `SetTouchMovement`, right stick Y → vertical, right stick X → snap yaw via `AddTouchLook`, left grip → climb. Target-ray poses feed `SetXrControllerRay` (camera-facing ribbons; skipped on Low). A DOM-overlay HUD shows the focused/nearest planet name when the optional `dom-overlay` feature is granted.
- Exit VR resumes the 2-D main loop and restores the previous quality preset.

## C++ stereo path

| Export | Role |
|--------|------|
| `SetXrSessionActive` | Toggle XR mode + pause/resume main loop |
| `SetXrBaseLayerFramebuffer` | GL name of the `XRWebGLLayer` framebuffer — the default draw target while a session runs |
| `SetXrEyeCount` / `SetXrEyeViewport` | Per-eye viewports from `XRWebGLLayer` |
| `GetXrMatrixScratch` + `CommitXrEyeMatrices` | JS writes view/proj (column-major float16×2) into WASM |
| `RunXrFrame` | One `RunOneFrame` without `glfwSwapBuffers` / FBO 0 |
| `GetCameraPositionX/Y/Z` | Fly-camera position for `view * T(-cam)` |

Per XR frame, C++ renders each eye with overridden view/projection (skybox uses the eye projection).

### The default framebuffer is not FBO 0 in VR

JS binds `XRWebGLLayer.framebuffer` before handing the frame to `RunXrFrame`, so any pass
that renders off-screen has to rebind *that* on the way out. A hardcoded
`glBindFramebuffer(GL_FRAMEBUFFER, 0)` sends the rest of the eye's geometry to the canvas
instead of the headset. `Application::DefaultFramebuffer()` is the single answer to
"where does the frame composite?": the XR layer framebuffer while `_xr.active`, 0
otherwise. Every former FBO-0 exit in `Application.cpp` / `SceneRenderer.cpp` goes through
it, and FBO constructors (`HDR`, `ShadowMapFBO`, `MagneticFieldBloom`) restore whatever was
bound on entry rather than 0.

The layer's framebuffer is an opaque `WebGLFramebuffer` the browser creates outside
Emscripten, so it has no GL name the WASM side can bind. `registerXrFramebuffer` in
`web/src/wasmBridge.ts` inserts it into Emscripten's `GL.framebuffers` table (which is why
`GL` is in `EXPORTED_RUNTIME_METHODS`) and `webxr.ts` pushes the resulting name through
`SetXrBaseLayerFramebuffer` whenever the base layer changes.

**Shadow maps now run in VR.** If registration fails, `baseLayerFramebuffer` stays 0,
`ShadowMapPass` falls back to skipping itself, and `RenderXrStereoFrame` logs the reason
once per session — rather than silently clobbering the stereo image.

HDR glow / lens flare are still skipped in VR (`RenderFrameContent`): their fullscreen
composite quad is sized to the 2-D display, not a per-eye viewport. Re-enabling them needs
per-eye HDR targets, which is out of scope here.

## Context creation

`Module.contextAttributes.xrCompatible = true` is set from `web/src/main.ts` before GLFW creates the WebGL 2 context. MSAA is still fixed at context creation — prefer `?quality=medium` (0× MSAA) when targeting VR headsets; see [TESTING_GUIDE.md](TESTING_GUIDE.md) quality table.

## Three.js companion spike

`web/threejs/` can host a faster UX spike via Three's `WebXRManager` / `VRButton` on the **WebGLRenderer fallback** path (WebGPU XR support is still uneven). The WASM path above is the acceptance target for the main solar-system app.

## Manual check

1. Chrome + WebXR emulator **or** a Quest browser on HTTPS/`localhost`.
2. Open `/solar-system/`, wait for load, confirm **Enter VR** appears.
3. Enter VR → stereoscopic view, head tracking, stick flight.
4. Exit VR → 2-D canvas resumes; overlays visible again.
5. Firefox/Safari without immersive-vr → no button, no console errors beyond a single availability log.
