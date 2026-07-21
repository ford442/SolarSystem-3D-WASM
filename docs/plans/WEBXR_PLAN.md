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
- Controllers: left stick → `SetTouchMovement`, right stick Y → vertical, right stick X → snap yaw via `AddTouchLook`, left grip → climb.
- Exit VR resumes the 2-D main loop and restores the previous quality preset.

## C++ stereo path

| Export | Role |
|--------|------|
| `SetXrSessionActive` | Toggle XR mode + pause/resume main loop |
| `SetXrEyeCount` / `SetXrEyeViewport` | Per-eye viewports from `XRWebGLLayer` |
| `GetXrMatrixScratch` + `CommitXrEyeMatrices` | JS writes view/proj (column-major float16×2) into WASM |
| `RunXrFrame` | One `RunOneFrame` without `glfwSwapBuffers` / FBO 0 |
| `GetCameraPositionX/Y/Z` | Fly-camera position for `view * T(-cam)` |

Per XR frame, C++ renders each eye with overridden view/projection (skybox uses the eye projection). Shadow-map passes are **skipped in VR** so they cannot rebind FBO 0 over the XR layer. HDR glow / lens flare are also skipped in VR for the same reason.

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
