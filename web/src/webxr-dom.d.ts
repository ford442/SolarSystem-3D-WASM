/**
 * Minimal WebXR DOM typings for browsers that expose navigator.xr.
 * Full @types/webxr is optional; these cover the immersive-vr surface we use.
 */

interface XRSession extends EventTarget {
  inputSources: readonly XRInputSource[];
  renderState: XRRenderState;
  updateRenderState(state: XRRenderStateInit): Promise<void>;
  requestReferenceSpace(type: XRReferenceSpaceType): Promise<XRReferenceSpace>;
  requestAnimationFrame(callback: XRFrameRequestCallback): number;
  cancelAnimationFrame(handle: number): void;
  end(): Promise<void>;
  addEventListener(type: 'end', listener: (event: Event) => void): void;
}

interface XRRenderState {
  baseLayer: XRWebGLLayer | null;
  depthNear: number;
  depthFar: number;
}

interface XRRenderStateInit {
  baseLayer?: XRWebGLLayer | null;
  depthNear?: number;
  depthFar?: number;
}

type XRReferenceSpaceType = 'local' | 'local-floor' | 'bounded-floor' | 'unbounded' | 'viewer';

interface XRReferenceSpace extends EventTarget {}

interface XRFrame {
  session: XRSession;
  getViewerPose(referenceSpace: XRReferenceSpace): XRViewerPose | null;
}

interface XRViewerPose {
  views: readonly XRView[];
}

interface XRView {
  eye: 'left' | 'right' | 'none';
  projectionMatrix: Float32Array;
  transform: XRRigidTransform;
}

interface XRRigidTransform {
  matrix: Float32Array;
  inverse: XRRigidTransform;
}

interface XRWebGLLayerInit {
  antialias?: boolean;
  depth?: boolean;
  stencil?: boolean;
  alpha?: boolean;
  ignoreDepthValues?: boolean;
  framebufferScaleFactor?: number;
}

declare class XRWebGLLayer {
  constructor(session: XRSession, context: WebGLRenderingContext | WebGL2RenderingContext, layerInit?: XRWebGLLayerInit);
  readonly framebuffer: WebGLFramebuffer | null;
  getViewport(view: XRView): XRViewport | null;
}

interface XRViewport {
  x: number;
  y: number;
  width: number;
  height: number;
}

type XRHandedness = 'none' | 'left' | 'right';

interface XRInputSource {
  handedness: XRHandedness;
  gamepad: Gamepad | null;
}

type XRFrameRequestCallback = (time: DOMHighResTimeStamp, frame: XRFrame) => void;

interface XRSessionInit {
  requiredFeatures?: string[];
  optionalFeatures?: string[];
}

interface XRSystem {
  isSessionSupported(mode: 'inline' | 'immersive-vr' | 'immersive-ar'): Promise<boolean>;
  requestSession(mode: 'inline' | 'immersive-vr' | 'immersive-ar', options?: XRSessionInit): Promise<XRSession>;
}

interface Navigator {
  readonly xr?: XRSystem;
}

interface WebGL2RenderingContext {
  makeXRCompatible(): Promise<void>;
}
