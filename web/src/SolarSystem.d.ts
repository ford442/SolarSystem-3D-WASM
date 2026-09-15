/** Teleport the camera and update its yaw/pitch orientation, in degrees. */
export type SetCameraPose = (
  x: number,
  y: number,
  z: number,
  yaw: number,
  pitch: number,
) => void;

export type QualityPreset = 0 | 1 | 2;

/** Apply a quality preset: 0=low, 1=medium, 2=full. */
export type SetQualityPreset = (preset: QualityPreset) => void;
/** Read the active quality preset from the C++ runtime. */
export type GetQualityPreset = () => QualityPreset;

/** Set the orbital animation multiplier, clamped by C++ to 0.01x-10000x. */
export type SetTimeScale = (scale: number) => void;
export type GetTimeScale = () => number;

/** Pause or resume orbital animation. */
export type SetPaused = (paused: boolean) => void;
/** Raw C export returns 0/1; wasmBridge façade returns boolean. */
export type GetPaused = () => boolean;

/** Set the simulation epoch as a Julian Date (fractional day allowed). */
export type SetSimulationEpoch = (julianDate: number) => void;
/** Read the active simulation Julian Date from the C++ runtime. */
export type GetSimulationEpoch = () => number;

export type ShadowQuality = 0 | 1 | 2 | 3;
/** Set shadow quality: 0=off, 1=low, 2=medium, 3=full. */
export type SetShadowQuality = (quality: ShadowQuality) => void;
export type GetShadowQuality = () => ShadowQuality;

export type SetTouchMovement = (forward: number, right: number, vertical: number) => void;
export type AddTouchLook = (deltaX: number, deltaY: number) => void;
export type AddTouchZoom = (delta: number) => void;

export type PlanetIndex = 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11;

export type OrbitScaleMode = 0 | 1;

export type SetMusicVolume = (volume: number) => void;
export type GetMusicVolume = () => number;
export type SetMusicMuted = (muted: boolean) => void;
export type GetMusicMuted = () => boolean;

/** Planet index accepted by FocusPlanet: 0=sun, 1=Mercury, ..., 9=Pluto, 10=Ceres, 11=Vesta. */
export type FocusPlanet = (index: PlanetIndex) => void;

/** 0=compressed artistic orbits, 1=AU-proportional spacing. */
export type SetOrbitScaleMode = (mode: OrbitScaleMode) => void;
export type GetOrbitScaleMode = () => OrbitScaleMode;
/** Returns 1–11 for nearest loaded planet, or -1 when unknown. */
export type GetNearestPlanetIndex = () => number;
export type GetFocusedPlanetIndex = () => number;
export type GetPlanetSceneDistance = (index: PlanetIndex) => number;

/** Toggle faint heliocentric orbit path lines. */
export type SetOrbitLines = (enabled: boolean) => void;
export type GetOrbitLines = () => boolean;
/** Toggle magnetic field ribbon overlay. */
export type SetMagneticFields = (enabled: boolean) => void;
export type GetMagneticFields = () => boolean;
/** Issue #107 aliases for Set/GetMagneticFields. */
export type SetMagneticFieldMode = SetMagneticFields;
export type GetMagneticFieldMode = GetMagneticFields;

export type SetXrSessionActive = (active: boolean) => void;
export type SetXrEyeCount = (count: number) => void;
export type SetXrEyeViewport = (
  eye: number,
  x: number,
  y: number,
  width: number,
  height: number,
) => void;
export type GetXrMatrixScratch = () => number;
export type CommitXrEyeMatrices = (eye: number) => void;
export type RunXrFrame = () => void;
export type GetCameraPositionComponent = () => number;
export type GetCameraYaw = () => number;
export type GetCameraPitch = () => number;

export interface SolarSystemCwrap {
  // BEGIN GENERATED CWARP OVERLOADS
  (
    ident: 'SetCameraPose',
    returnType: null,
    argTypes: ['number', 'number', 'number', 'number', 'number'],
  ): (...args: number[]) => void;
  (
    ident: 'SetQualityPreset',
    returnType: null,
    argTypes: ['number'],
  ): (...args: number[]) => void;
  (
    ident: 'GetQualityPreset',
    returnType: 'number',
    argTypes: [],
  ): (...args: number[]) => number;
  (
    ident: 'SetTimeScale',
    returnType: null,
    argTypes: ['number'],
  ): (...args: number[]) => void;
  (
    ident: 'GetTimeScale',
    returnType: 'number',
    argTypes: [],
  ): (...args: number[]) => number;
  (
    ident: 'SetPaused',
    returnType: null,
    argTypes: ['number'],
  ): (...args: number[]) => void;
  (
    ident: 'GetPaused',
    returnType: 'number',
    argTypes: [],
  ): (...args: number[]) => number;
  (
    ident: 'SetSimulationEpoch',
    returnType: null,
    argTypes: ['number'],
  ): (...args: number[]) => void;
  (
    ident: 'GetSimulationEpoch',
    returnType: 'number',
    argTypes: [],
  ): (...args: number[]) => number;
  (
    ident: 'SetShadowQuality',
    returnType: null,
    argTypes: ['number'],
  ): (...args: number[]) => void;
  (
    ident: 'GetShadowQuality',
    returnType: 'number',
    argTypes: [],
  ): (...args: number[]) => number;
  (
    ident: 'SetTouchMovement',
    returnType: null,
    argTypes: ['number', 'number', 'number'],
  ): (...args: number[]) => void;
  (
    ident: 'AddTouchLook',
    returnType: null,
    argTypes: ['number', 'number'],
  ): (...args: number[]) => void;
  (
    ident: 'AddTouchZoom',
    returnType: null,
    argTypes: ['number'],
  ): (...args: number[]) => void;
  (
    ident: 'IsMobileWeb',
    returnType: 'number',
    argTypes: [],
  ): (...args: number[]) => number;
  (
    ident: 'SetMusicVolume',
    returnType: null,
    argTypes: ['number'],
  ): (...args: number[]) => void;
  (
    ident: 'GetMusicVolume',
    returnType: 'number',
    argTypes: [],
  ): (...args: number[]) => number;
  (
    ident: 'SetMusicMuted',
    returnType: null,
    argTypes: ['number'],
  ): (...args: number[]) => void;
  (
    ident: 'GetMusicMuted',
    returnType: 'number',
    argTypes: [],
  ): (...args: number[]) => number;
  (
    ident: 'FocusPlanet',
    returnType: null,
    argTypes: ['number'],
  ): (...args: number[]) => void;
  (
    ident: 'SetOrbitScaleMode',
    returnType: null,
    argTypes: ['number'],
  ): (...args: number[]) => void;
  (
    ident: 'GetOrbitScaleMode',
    returnType: 'number',
    argTypes: [],
  ): (...args: number[]) => number;
  (
    ident: 'GetNearestPlanetIndex',
    returnType: 'number',
    argTypes: [],
  ): (...args: number[]) => number;
  (
    ident: 'GetFocusedPlanetIndex',
    returnType: 'number',
    argTypes: [],
  ): (...args: number[]) => number;
  (
    ident: 'GetPlanetSceneDistance',
    returnType: 'number',
    argTypes: ['number'],
  ): (...args: number[]) => number;
  (
    ident: 'GetNextConjunctionJulianDate',
    returnType: 'number',
    argTypes: [],
  ): (...args: number[]) => number;
  (
    ident: 'GetNextConjunctionBodyA',
    returnType: 'number',
    argTypes: [],
  ): (...args: number[]) => number;
  (
    ident: 'GetNextConjunctionBodyB',
    returnType: 'number',
    argTypes: [],
  ): (...args: number[]) => number;
  (
    ident: 'GetNextConjunctionSeparationDeg',
    returnType: 'number',
    argTypes: [],
  ): (...args: number[]) => number;
  (
    ident: 'GetNextSkyEventJson',
    returnType: 'string',
    argTypes: [],
  ): (...args: number[]) => string;
  (
    ident: 'SetOrbitLines',
    returnType: null,
    argTypes: ['number'],
  ): (...args: number[]) => void;
  (
    ident: 'GetOrbitLines',
    returnType: 'number',
    argTypes: [],
  ): (...args: number[]) => number;
  (
    ident: 'SetMagneticFields',
    returnType: null,
    argTypes: ['number'],
  ): (...args: number[]) => void;
  (
    ident: 'GetMagneticFields',
    returnType: 'number',
    argTypes: [],
  ): (...args: number[]) => number;
  (
    ident: 'SetMagneticFieldMode',
    returnType: null,
    argTypes: ['number'],
  ): (...args: number[]) => void;
  (
    ident: 'GetMagneticFieldMode',
    returnType: 'number',
    argTypes: [],
  ): (...args: number[]) => number;
  (
    ident: 'SetXrSessionActive',
    returnType: null,
    argTypes: ['number'],
  ): (...args: number[]) => void;
  (
    ident: 'SetXrBaseLayerFramebuffer',
    returnType: null,
    argTypes: ['number'],
  ): (...args: number[]) => void;
  (
    ident: 'SetXrEyeCount',
    returnType: null,
    argTypes: ['number'],
  ): (...args: number[]) => void;
  (
    ident: 'SetXrEyeViewport',
    returnType: null,
    argTypes: ['number', 'number', 'number', 'number', 'number'],
  ): (...args: number[]) => void;
  (
    ident: 'GetXrMatrixScratch',
    returnType: 'number',
    argTypes: [],
  ): (...args: number[]) => number;
  (
    ident: 'CommitXrEyeMatrices',
    returnType: null,
    argTypes: ['number'],
  ): (...args: number[]) => void;
  (
    ident: 'RunXrFrame',
    returnType: null,
    argTypes: [],
  ): (...args: number[]) => void;
  (
    ident: 'GetCameraPositionX',
    returnType: 'number',
    argTypes: [],
  ): (...args: number[]) => number;
  (
    ident: 'GetCameraPositionY',
    returnType: 'number',
    argTypes: [],
  ): (...args: number[]) => number;
  (
    ident: 'GetCameraPositionZ',
    returnType: 'number',
    argTypes: [],
  ): (...args: number[]) => number;
  (
    ident: 'GetCameraYaw',
    returnType: 'number',
    argTypes: [],
  ): (...args: number[]) => number;
  (
    ident: 'GetCameraPitch',
    returnType: 'number',
    argTypes: [],
  ): (...args: number[]) => number;
  // END GENERATED CWARP OVERLOADS
}

export type SettingsChangeField =
  | 'quality'
  | 'timeScale'
  | 'paused'
  | 'shadowQuality'
  | 'musicVolume'
  | 'musicMuted'
  | 'simulationEpoch'
  | 'orbitLines'
  | 'magneticFields'
  | 'magneticFieldMode';

export interface SolarSystemModuleConfig {
  canvas: HTMLCanvasElement;
  locateFile?: (path: string, prefix: string) => string;
  print?: (text: string) => void;
  printErr?: (text: string) => void;
  onRuntimeInitialized?: () => void;
  /** C++ → JS callbacks registered on Module before runtime init. */
  updateLoadingProgress?: (loaded: number, total: number) => void;
  updateStreamingProgress?: (completed: number, total: number, active?: number, tierCode?: number) => void;
  onPlanetFocused?: (index: number) => void;
  onSettingsChanged?: (field: SettingsChangeField | string) => void;
  /**
   * A WebGL2 context created by the caller (see web/src/webglContext.ts), reused as-is by
   * Emscripten's GLFW port. This is the ONLY way to control powerPreference / xrCompatible /
   * premultipliedAlpha / preserveDrawingBuffer / alpha / antialias here — GLFW builds its own
   * context-attributes object from GLFW window hints and ignores any other Module config key
   * for this purpose (there used to be a `contextAttributes` field on this interface; it was
   * never read by Emscripten's runtime and has been removed).
   */
  preinitializedWebGLContext?: WebGL2RenderingContext;
}

/**
 * The slice of Emscripten's GL runtime object we use. Exported from the module by
 * `-sEXPORTED_RUNTIME_METHODS=[...,'GL']` (see CMakeLists.txt). WebGL objects the browser
 * creates outside Emscripten — notably `XRWebGLLayer.framebuffer` — have no GL name the
 * WASM side can bind, so they must be inserted into this table first.
 */
export interface EmscriptenGL {
  framebuffers: (WebGLFramebuffer | null)[];
  getNewId: (table: unknown[]) => number;
}

export interface SolarSystemModule {
  canvas: HTMLCanvasElement;
  cwrap: SolarSystemCwrap;
  HEAPF32: Float32Array;
  HEAP8: Int8Array;
  GL?: EmscriptenGL;
  _main: (argc: number, argv: number) => number;
  // BEGIN GENERATED MODULE EXPORTS
  _SetCameraPose: (...args: number[]) => void;
  _SetQualityPreset: (...args: number[]) => void;
  _GetQualityPreset: () => number;
  _SetTimeScale: (...args: number[]) => void;
  _GetTimeScale: () => number;
  _SetPaused: (...args: number[]) => void;
  _GetPaused: () => number;
  _SetSimulationEpoch: (...args: number[]) => void;
  _GetSimulationEpoch: () => number;
  _SetShadowQuality: (...args: number[]) => void;
  _GetShadowQuality: () => number;
  _SetTouchMovement: (...args: number[]) => void;
  _AddTouchLook: (...args: number[]) => void;
  _AddTouchZoom: (...args: number[]) => void;
  _IsMobileWeb: () => number;
  _SetMusicVolume: (...args: number[]) => void;
  _GetMusicVolume: () => number;
  _SetMusicMuted: (...args: number[]) => void;
  _GetMusicMuted: () => number;
  _FocusPlanet: (...args: number[]) => void;
  _SetOrbitScaleMode: (...args: number[]) => void;
  _GetOrbitScaleMode: () => number;
  _GetNearestPlanetIndex: () => number;
  _GetFocusedPlanetIndex: () => number;
  _GetPlanetSceneDistance: (...args: number[]) => number;
  _GetNextConjunctionJulianDate: () => number;
  _GetNextConjunctionBodyA: () => number;
  _GetNextConjunctionBodyB: () => number;
  _GetNextConjunctionSeparationDeg: () => number;
  _GetNextSkyEventJson: () => number;
  _SetOrbitLines: (...args: number[]) => void;
  _GetOrbitLines: () => number;
  _SetMagneticFields: (...args: number[]) => void;
  _GetMagneticFields: () => number;
  _SetMagneticFieldMode: (...args: number[]) => void;
  _GetMagneticFieldMode: () => number;
  _SetXrSessionActive: (...args: number[]) => void;
  _SetXrBaseLayerFramebuffer: (...args: number[]) => void;
  _SetXrEyeCount: (...args: number[]) => void;
  _SetXrEyeViewport: (...args: number[]) => void;
  _GetXrMatrixScratch: () => number;
  _CommitXrEyeMatrices: (...args: number[]) => void;
  _RunXrFrame: () => void;
  _GetCameraPositionX: () => number;
  _GetCameraPositionY: () => number;
  _GetCameraPositionZ: () => number;
  _GetCameraYaw: () => number;
  _GetCameraPitch: () => number;
  // END GENERATED MODULE EXPORTS
}

declare const SolarSystem: (
  config: SolarSystemModuleConfig,
) => Promise<SolarSystemModule>;

export default SolarSystem;

declare global {
  interface Window {
    /** Console helpers documented in AGENTS.md. */
    setCameraPose?: SetCameraPose;
    setQualityPreset?: SetQualityPreset;
    getQualityPreset?: GetQualityPreset;
    getNextConjunction?: () => {
      valid: boolean;
      bodyA: number;
      bodyB: number;
      julianDate: number;
      separationDeg: number;
    };
    /** Runtime asset base URL for WebResourceFetcher. */
    __solarSystemAssetBase?: string;
    /** Init config published before Module() — read by QualitySettings.cpp. */
    __solarSystemInit?: {
      qualityPreset: QualityPreset;
      isMobileWeb: boolean;
      backingStoreScale: number;
    };
  }

  /** Incoming Module config (callbacks live on SolarSystemModuleConfig). */
  const Module: SolarSystemModuleConfig;
}
