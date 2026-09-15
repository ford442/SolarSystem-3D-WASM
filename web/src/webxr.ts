/**
 * Opt-in WebXR immersive-vr session for the WASM renderer.
 * Controllers map to SetTouchMovement / AddTouchLook; eye matrices are
 * pushed into C++ each XR frame for stereo rendering.
 */

export type XrBindings = {
  setTouchMovement: (forward: number, right: number, vertical: number) => void;
  addTouchLook: (deltaX: number, deltaY: number) => void;
  setQualityPreset: (preset: 0 | 1 | 2) => void;
  getQualityPreset: () => number;
  getCameraPosition: () => { x: number; y: number; z: number };
  getNearestPlanetIndex?: () => number;
  getFocusedPlanetIndex?: () => number;
  setXrSessionActive: (active: boolean) => void;
  setXrBaseLayerFramebuffer: (framebuffer: number) => void;
  registerXrFramebuffer: (framebuffer: WebGLFramebuffer | null) => number;
  setXrEyeCount: (count: number) => void;
  setXrEyeViewport: (eye: number, x: number, y: number, w: number, h: number) => void;
  commitXrEyeMatrices: (eye: number) => void;
  getXrMatrixScratchPtr: () => number;
  runXrFrame: () => void;
  getHeapF32: () => Float32Array;
  setXrControllerRay?: (
    hand: number,
    ox: number,
    oy: number,
    oz: number,
    dx: number,
    dy: number,
    dz: number,
    visible: boolean,
  ) => void;
};

export type WebXrController = {
  /** Tear down listeners / end session if active. */
  dispose: () => void;
  /** True when an immersive-vr session is running. */
  isPresenting: () => boolean;
};

const XR_DEPTH_NEAR = 0.001;
const XR_DEPTH_FAR = 20000;
const SNAP_TURN_DEG = 30;
const SNAP_TURN_COOLDOWN_MS = 350;

const XR_BODY_NAMES = [
  'Sun', 'Mercury', 'Venus', 'Earth', 'Mars', 'Jupiter', 'Saturn',
  'Uranus', 'Neptune', 'Pluto', 'Ceres', 'Vesta',
] as const;

function rotateVectorByQuat(
  x: number, y: number, z: number, w: number,
  vx: number, vy: number, vz: number,
): { x: number; y: number; z: number } {
  const ix = w * vx + y * vz - z * vy;
  const iy = w * vy + z * vx - x * vz;
  const iz = w * vz + x * vy - y * vx;
  const iw = -x * vx - y * vy - z * vz;
  return {
    x: ix * w + iw * -x + iy * z - iz * y,
    y: iy * w + iw * -y + iz * x - ix * z,
    z: iz * w + iw * -z + ix * y - iy * x,
  };
}

function multiplyMat4(a: Float32Array, b: Float32Array, out: Float32Array): void {
  const r = new Float32Array(16);
  for (let col = 0; col < 4; ++col) {
    for (let row = 0; row < 4; ++row) {
      r[col * 4 + row] =
        a[0 * 4 + row] * b[col * 4 + 0] +
        a[1 * 4 + row] * b[col * 4 + 1] +
        a[2 * 4 + row] * b[col * 4 + 2] +
        a[3 * 4 + row] * b[col * 4 + 3];
    }
  }
  out.set(r);
}

function translationMat4(x: number, y: number, z: number, out: Float32Array): void {
  out.fill(0);
  out[0] = 1;
  out[5] = 1;
  out[10] = 1;
  out[15] = 1;
  out[12] = x;
  out[13] = y;
  out[14] = z;
}

function clampAxis(v: number, deadzone = 0.15): number {
  if (Math.abs(v) < deadzone) {
    return 0;
  }
  const sign = v < 0 ? -1 : 1;
  return sign * Math.min(1, (Math.abs(v) - deadzone) / (1 - deadzone));
}

/**
 * Show #enter-vr when immersive-vr is available; hide otherwise.
 * Returns a controller, or null when WebXR is unavailable.
 */
export async function initWebXr(options: {
  canvas: HTMLCanvasElement;
  enterVrButton: HTMLButtonElement;
  exitVrButton?: HTMLButtonElement | null;
  overlayRoots?: HTMLElement[];
  hudRoot?: HTMLElement | null;
  tooltip?: HTMLElement | null;
  controllersRoot?: HTMLElement | null;
  bindings: XrBindings;
}): Promise<WebXrController | null> {
  const {
    canvas,
    enterVrButton,
    exitVrButton,
    overlayRoots = [],
    hudRoot = null,
    tooltip = null,
    controllersRoot = null,
    bindings,
  } = options;

  const xr = navigator.xr;
  if (!xr || typeof xr.isSessionSupported !== 'function') {
    enterVrButton.hidden = true;
    if (hudRoot) {
      hudRoot.hidden = true;
    }
    console.log('[WebXR] navigator.xr unavailable — staying 2D');
    return null;
  }

  console.log('[WebXR] Checking immersive-vr support…');
  let supported = false;
  try {
    supported = await Promise.race([
      xr.isSessionSupported('immersive-vr'),
      new Promise<boolean>((resolve) => {
        window.setTimeout(() => resolve(false), 2000);
      }),
    ]);
  } catch (error) {
    console.warn('[WebXR] isSessionSupported failed:', error);
    enterVrButton.hidden = true;
    return null;
  }

  if (!supported) {
    enterVrButton.hidden = true;
    if (hudRoot) {
      hudRoot.hidden = true;
    }
    console.log('[WebXR] immersive-vr not supported — staying 2D');
    return null;
  }

  enterVrButton.hidden = false;
  enterVrButton.disabled = false;

  let session: XRSession | null = null;
  let referenceSpace: XRReferenceSpace | null = null;
  let gl: WebGL2RenderingContext | null = null;
  let qualityBeforeVr: number | null = null;
  let lastSnapTurnMs = 0;
  let rafHandle = 0;
  // The XRWebGLLayer's framebuffer, once it has a name in Emscripten's GL table. C++
  // rebinds it after the shadow pass instead of FBO 0; without it the shadow pass is
  // skipped entirely (see Application::DefaultFramebuffer).
  let namedBaseLayerFramebuffer: WebGLFramebuffer | null = null;

  const scratchView = new Float32Array(16);
  const scratchPlayerInv = new Float32Array(16);
  const scratchCombined = new Float32Array(16);

  const setOverlaysVisible = (visible: boolean) => {
    for (const el of overlayRoots) {
      el.style.visibility = visible ? '' : 'hidden';
      el.style.pointerEvents = visible ? '' : 'none';
    }
    enterVrButton.hidden = !visible ? true : false;
    if (exitVrButton) {
      exitVrButton.hidden = visible;
    }
  };

  const endSession = async () => {
    if (session) {
      try {
        await session.end();
      } catch {
        /* already ended */
      }
    }
  };

  const onSessionEnded = () => {
    if (rafHandle) {
      // XR RAF cancels with session end; clear our handle.
      rafHandle = 0;
    }
    session = null;
    referenceSpace = null;
    namedBaseLayerFramebuffer = null;
    bindings.setTouchMovement(0, 0, 0);
    bindings.setXrControllerRay?.(0, 0, 0, 0, 0, 0, -1, false);
    bindings.setXrControllerRay?.(1, 0, 0, 0, 0, 0, -1, false);
    bindings.setXrSessionActive(false);
    if (qualityBeforeVr !== null) {
      bindings.setQualityPreset(qualityBeforeVr as 0 | 1 | 2);
      qualityBeforeVr = null;
    }
    setOverlaysVisible(true);
    enterVrButton.hidden = false;
    enterVrButton.textContent = 'Enter VR';
    enterVrButton.disabled = false;
    if (hudRoot) {
      hudRoot.hidden = true;
    }
    console.log('[WebXR] Session ended — resumed 2D main loop');
  };

  const pollControllers = (frame: XRFrame) => {
    if (!session || !referenceSpace) {
      return;
    }
    let forward = 0;
    let right = 0;
    let vertical = 0;
    let lookX = 0;
    let leftConnected = false;
    let rightConnected = false;
    const cam = bindings.getCameraPosition();

    for (const source of session.inputSources) {
      const pad = source.gamepad;
      const handedness = source.handedness;
      const handIndex = handedness === 'right' ? 1 : 0;
      if (handedness === 'right') {
        rightConnected = true;
      } else {
        leftConnected = true;
      }

      const rayPose = frame.getPose?.(source.targetRaySpace, referenceSpace) ?? null;
      if (rayPose && bindings.setXrControllerRay) {
        const pos = rayPose.transform.position;
        const ori = rayPose.transform.orientation;
        const dir = rotateVectorByQuat(ori.x, ori.y, ori.z, ori.w, 0, 0, -1);
        bindings.setXrControllerRay(
          handIndex,
          cam.x + pos.x,
          cam.y + pos.y,
          cam.z + pos.z,
          dir.x,
          dir.y,
          dir.z,
          true,
        );
      }

      if (!pad) {
        continue;
      }
      // xr-standard: axes 2/3 = thumbstick; fall back to 0/1.
      const ax = clampAxis(pad.axes[2] ?? pad.axes[0] ?? 0);
      const ay = clampAxis(pad.axes[3] ?? pad.axes[1] ?? 0);

      if (handedness === 'left' || handedness === 'none') {
        // Match touch joystick: forward = -Y, right = +X
        forward += -ay;
        right += ax;
        if (pad.buttons[1]?.pressed) {
          vertical += 0.85;
        }
      } else if (handedness === 'right') {
        vertical += -ay;
        const now = performance.now();
        if (Math.abs(ax) > 0.7 && now - lastSnapTurnMs > SNAP_TURN_COOLDOWN_MS) {
          lookX = ax > 0 ? SNAP_TURN_DEG : -SNAP_TURN_DEG;
          lastSnapTurnMs = now;
        }
      }
    }

    if (!leftConnected) {
      bindings.setXrControllerRay?.(0, 0, 0, 0, 0, 0, -1, false);
    }
    if (!rightConnected) {
      bindings.setXrControllerRay?.(1, 0, 0, 0, 0, 0, -1, false);
    }
    if (controllersRoot) {
      controllersRoot.hidden = !(leftConnected || rightConnected);
      const leftEl = controllersRoot.querySelector('[data-hand="left"]');
      const rightEl = controllersRoot.querySelector('[data-hand="right"]');
      if (leftEl instanceof HTMLElement) {
        leftEl.classList.toggle('is-active', leftConnected);
      }
      if (rightEl instanceof HTMLElement) {
        rightEl.classList.toggle('is-active', rightConnected);
      }
    }

    const focused = bindings.getFocusedPlanetIndex?.() ?? -1;
    const nearest = bindings.getNearestPlanetIndex?.() ?? -1;
    const bodyIndex = focused >= 0 ? focused : nearest;
    if (tooltip) {
      tooltip.textContent = bodyIndex >= 0 && bodyIndex < XR_BODY_NAMES.length
        ? XR_BODY_NAMES[bodyIndex]
        : '';
    }

    const mag = Math.hypot(forward, right, vertical);
    if (mag > 1) {
      forward /= mag;
      right /= mag;
      vertical /= mag;
    }
    bindings.setTouchMovement(forward, right, vertical);
    if (lookX !== 0) {
      bindings.addTouchLook(lookX, 0);
    }
  };

  const onXrFrame = (time: number, frame: XRFrame) => {
    if (!session || !referenceSpace || !gl) {
      return;
    }
    rafHandle = session.requestAnimationFrame(onXrFrame);

    const pose = frame.getViewerPose(referenceSpace);
    const layer = session.renderState.baseLayer;
    if (!pose || !layer) {
      return;
    }

    pollControllers(frame);

    if (layer.framebuffer !== namedBaseLayerFramebuffer) {
      // A session can swap its base layer; re-register whenever it changes.
      namedBaseLayerFramebuffer = layer.framebuffer;
      bindings.setXrBaseLayerFramebuffer(bindings.registerXrFramebuffer(layer.framebuffer));
    }

    gl.bindFramebuffer(gl.FRAMEBUFFER, layer.framebuffer);
    gl.clearColor(0, 0, 0, 1);
    gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);

    const cam = bindings.getCameraPosition();
    translationMat4(-cam.x, -cam.y, -cam.z, scratchPlayerInv);

    const views = pose.views;
    bindings.setXrEyeCount(views.length);

    const scratchBase = bindings.getXrMatrixScratchPtr() >> 2;

    for (let eye = 0; eye < views.length && eye < 2; ++eye) {
      const view = views[eye];
      const viewport = layer.getViewport(view);
      if (!viewport) {
        continue;
      }
      bindings.setXrEyeViewport(eye, viewport.x, viewport.y, viewport.width, viewport.height);

      // view.transform.inverse.matrix: stage → eye (column-major).
      // Combined with T(-camera) so touch/controller flight still moves the stage.
      const xrView = view.transform.inverse.matrix as Float32Array;
      scratchView.set(xrView);
      multiplyMat4(scratchView, scratchPlayerInv, scratchCombined);

      const proj = view.projectionMatrix as Float32Array;
      const heap = bindings.getHeapF32();
      heap.set(scratchCombined, scratchBase);
      heap.set(proj, scratchBase + 16);
      bindings.commitXrEyeMatrices(eye);
    }

    bindings.runXrFrame();
    void time;
  };

  const startSession = async () => {
    if (session) {
      return;
    }
    enterVrButton.disabled = true;
    enterVrButton.textContent = 'Entering VR…';

    try {
      gl = canvas.getContext('webgl2');
      if (!gl) {
        throw new Error('WebGL2 context not available on canvas');
      }
      if (typeof gl.makeXRCompatible === 'function') {
        await gl.makeXRCompatible();
      }

      if (hudRoot) {
        hudRoot.hidden = false;
      }
      const sessionOptions: XRSessionInit = {
        requiredFeatures: ['local-floor'],
        optionalFeatures: ['local', 'bounded-floor', 'dom-overlay'],
        ...(hudRoot ? { domOverlay: { root: hudRoot } } : {}),
      };
      try {
        session = await xr.requestSession('immersive-vr', sessionOptions);
      } catch (overlayError) {
        console.warn('[WebXR] Session with DOM overlay failed, retrying without it:', overlayError);
        session = await xr.requestSession('immersive-vr', {
          requiredFeatures: ['local-floor'],
          optionalFeatures: ['local', 'bounded-floor'],
        });
      }

      const layer = new XRWebGLLayer(session, gl, {
        antialias: false,
        framebufferScaleFactor: 1.0,
      });
      await session.updateRenderState({
        baseLayer: layer,
        depthNear: XR_DEPTH_NEAR,
        depthFar: XR_DEPTH_FAR,
      });

      referenceSpace =
        (await session.requestReferenceSpace('local-floor').catch(() => null)) ||
        (await session.requestReferenceSpace('local'));

      qualityBeforeVr = bindings.getQualityPreset();
      // Prefer medium in VR (low if already low); MSAA is fixed at context creation.
      const vrPreset = (qualityBeforeVr <= 0 ? 0 : 1) as 0 | 1 | 2;
      bindings.setQualityPreset(vrPreset);

      session.addEventListener('end', onSessionEnded);
      bindings.setXrSessionActive(true);
      setOverlaysVisible(false);
      if (hudRoot) {
        hudRoot.hidden = false;
      }
      if (exitVrButton) {
        exitVrButton.hidden = false;
      }
      enterVrButton.textContent = 'Enter VR';
      console.log('[WebXR] immersive-vr session started (quality preset', vrPreset, ')');

      rafHandle = session.requestAnimationFrame(onXrFrame);
    } catch (error) {
      console.error('[WebXR] Failed to enter VR:', error);
      enterVrButton.disabled = false;
      enterVrButton.textContent = 'Enter VR';
      session = null;
      bindings.setXrSessionActive(false);
    }
  };

  enterVrButton.addEventListener('click', (event) => {
    event.stopPropagation();
    void startSession();
  });
  for (const eventName of ['pointerdown', 'pointerup', 'keydown', 'keyup'] as const) {
    enterVrButton.addEventListener(eventName, (event) => event.stopPropagation());
  }

  if (exitVrButton) {
    exitVrButton.hidden = true;
    exitVrButton.addEventListener('click', (event) => {
      event.stopPropagation();
      void endSession();
    });
    for (const eventName of ['pointerdown', 'pointerup', 'keydown', 'keyup'] as const) {
      exitVrButton.addEventListener(eventName, (event) => event.stopPropagation());
    }
  }

  return {
    dispose: () => {
      void endSession();
    },
    isPresenting: () => session !== null,
  };
}
