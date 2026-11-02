import type { PlanetIndex } from './generated/planetIds.js';

/**
 * Raw C export signatures are generated from WasmExports.cpp into the marked blocks below
 * (scripts/generate-wasm-exports.mjs). What stays hand-written here is only the vocabulary
 * the “branded” wasmBridge façade narrows those raw numbers to.
 */

/** Focus index of a body (Sun … 1P/Halley); generated from planets.catalog.json. */
export type { PlanetIndex };

export type QualityPreset = 0 | 1 | 2;

export type ShadowQuality = 0 | 1 | 2 | 3;

/** 0=compressed artistic orbits, 1=AU-proportional spacing. */
export type OrbitScaleMode = 0 | 1;

export interface SolarSystemCwrap {
  // BEGIN GENERATED CWARP OVERLOADS
  (
    ident: 'SetCameraPose',
    returnType: null,
    argTypes: ['number', 'number', 'number', 'number', 'number'],
  ): (x: number, y: number, z: number, yaw: number, pitch: number) => void;
  (
    ident: 'SetQualityPreset',
    returnType: null,
    argTypes: ['number'],
  ): (preset: number) => void;
  (
    ident: 'GetQualityPreset',
    returnType: 'number',
    argTypes: [],
  ): () => number;
  (
    ident: 'SetTimeScale',
    returnType: null,
    argTypes: ['number'],
  ): (scale: number) => void;
  (
    ident: 'GetTimeScale',
    returnType: 'number',
    argTypes: [],
  ): () => number;
  (
    ident: 'SetPaused',
    returnType: null,
    argTypes: ['number'],
  ): (paused: number) => void;
  (
    ident: 'GetPaused',
    returnType: 'number',
    argTypes: [],
  ): () => number;
  (
    ident: 'SetSimulationEpoch',
    returnType: null,
    argTypes: ['number'],
  ): (julianDate: number) => void;
  (
    ident: 'GetSimulationEpoch',
    returnType: 'number',
    argTypes: [],
  ): () => number;
  (
    ident: 'SetShadowQuality',
    returnType: null,
    argTypes: ['number'],
  ): (quality: number) => void;
  (
    ident: 'GetShadowQuality',
    returnType: 'number',
    argTypes: [],
  ): () => number;
  (
    ident: 'SetTouchMovement',
    returnType: null,
    argTypes: ['number', 'number', 'number'],
  ): (forward: number, right: number, vertical: number) => void;
  (
    ident: 'AddTouchLook',
    returnType: null,
    argTypes: ['number', 'number'],
  ): (deltaX: number, deltaY: number) => void;
  (
    ident: 'AddTouchZoom',
    returnType: null,
    argTypes: ['number'],
  ): (delta: number) => void;
  (
    ident: 'IsMobileWeb',
    returnType: 'number',
    argTypes: [],
  ): () => number;
  (
    ident: 'FocusPlanet',
    returnType: null,
    argTypes: ['number'],
  ): (idx: number) => void;
  (
    ident: 'FocusMission',
    returnType: null,
    argTypes: ['number'],
  ): (idx: number) => void;
  (
    ident: 'GetFocusedMissionIndex',
    returnType: 'number',
    argTypes: [],
  ): () => number;
  (
    ident: 'GetMissionCount',
    returnType: 'number',
    argTypes: [],
  ): () => number;
  (
    ident: 'GetMissionCatalogJson',
    returnType: 'string',
    argTypes: [],
  ): () => string;
  (
    ident: 'GetFocusedMissionJson',
    returnType: 'string',
    argTypes: [],
  ): () => string;
  (
    ident: 'SetOrbitScaleMode',
    returnType: null,
    argTypes: ['number'],
  ): (mode: number) => void;
  (
    ident: 'GetOrbitScaleMode',
    returnType: 'number',
    argTypes: [],
  ): () => number;
  (
    ident: 'GetNearestPlanetIndex',
    returnType: 'number',
    argTypes: [],
  ): () => number;
  (
    ident: 'GetFocusedPlanetIndex',
    returnType: 'number',
    argTypes: [],
  ): () => number;
  (
    ident: 'GetPlanetSceneDistance',
    returnType: 'number',
    argTypes: ['number'],
  ): (idx: number) => number;
  (
    ident: 'GetNextConjunctionJulianDate',
    returnType: 'number',
    argTypes: [],
  ): () => number;
  (
    ident: 'GetNextConjunctionBodyA',
    returnType: 'number',
    argTypes: [],
  ): () => number;
  (
    ident: 'GetNextConjunctionBodyB',
    returnType: 'number',
    argTypes: [],
  ): () => number;
  (
    ident: 'GetNextConjunctionSeparationDeg',
    returnType: 'number',
    argTypes: [],
  ): () => number;
  (
    ident: 'GetNextSkyEventJson',
    returnType: 'string',
    argTypes: [],
  ): () => string;
  (
    ident: 'SetObserveMode',
    returnType: null,
    argTypes: ['number'],
  ): (enabled: number) => void;
  (
    ident: 'GetObserveMode',
    returnType: 'number',
    argTypes: [],
  ): () => number;
  (
    ident: 'SetObserverSite',
    returnType: null,
    argTypes: ['number', 'number', 'number'],
  ): (latDeg: number, lonDeg: number, altM: number) => void;
  (
    ident: 'SetObserveView',
    returnType: null,
    argTypes: ['number', 'number'],
  ): (azimuthDeg: number, elevationDeg: number) => void;
  (
    ident: 'SetObserveFov',
    returnType: null,
    argTypes: ['number'],
  ): (fovDeg: number) => void;
  (
    ident: 'SetObserveTimeRate',
    returnType: null,
    argTypes: ['number'],
  ): (rate: number) => void;
  (
    ident: 'SetObserveConstellations',
    returnType: null,
    argTypes: ['number'],
  ): (visible: number) => void;
  (
    ident: 'GetObserveStateJson',
    returnType: 'string',
    argTypes: [],
  ): () => string;
  (
    ident: 'SetOrbitLines',
    returnType: null,
    argTypes: ['number'],
  ): (enabled: number) => void;
  (
    ident: 'GetOrbitLines',
    returnType: 'number',
    argTypes: [],
  ): () => number;
  (
    ident: 'SetMagneticFields',
    returnType: null,
    argTypes: ['number'],
  ): (enabled: number) => void;
  (
    ident: 'GetMagneticFields',
    returnType: 'number',
    argTypes: [],
  ): () => number;
  (
    ident: 'SetMagneticFieldMode',
    returnType: null,
    argTypes: ['number'],
  ): (enabled: number) => void;
  (
    ident: 'GetMagneticFieldMode',
    returnType: 'number',
    argTypes: [],
  ): () => number;
  (
    ident: 'SetXrSessionActive',
    returnType: null,
    argTypes: ['number'],
  ): (active: number) => void;
  (
    ident: 'SetXrBaseLayerFramebuffer',
    returnType: null,
    argTypes: ['number'],
  ): (framebuffer: number) => void;
  (
    ident: 'SetXrEyeCount',
    returnType: null,
    argTypes: ['number'],
  ): (count: number) => void;
  (
    ident: 'SetXrEyeViewport',
    returnType: null,
    argTypes: ['number', 'number', 'number', 'number', 'number'],
  ): (eye: number, x: number, y: number, width: number, height: number) => void;
  (
    ident: 'GetXrMatrixScratch',
    returnType: 'number',
    argTypes: [],
  ): () => number;
  (
    ident: 'CommitXrEyeMatrices',
    returnType: null,
    argTypes: ['number'],
  ): (eye: number) => void;
  (
    ident: 'RunXrFrame',
    returnType: null,
    argTypes: [],
  ): () => void;
  (
    ident: 'SetXrControllerRay',
    returnType: null,
    argTypes: ['number', 'number', 'number', 'number', 'number', 'number', 'number', 'number'],
  ): (hand: number, ox: number, oy: number, oz: number, dx: number, dy: number, dz: number, visible: number) => void;
  (
    ident: 'GetCameraPositionX',
    returnType: 'number',
    argTypes: [],
  ): () => number;
  (
    ident: 'GetCameraPositionY',
    returnType: 'number',
    argTypes: [],
  ): () => number;
  (
    ident: 'GetCameraPositionZ',
    returnType: 'number',
    argTypes: [],
  ): () => number;
  (
    ident: 'GetCameraHeliocentricX',
    returnType: 'number',
    argTypes: [],
  ): () => number;
  (
    ident: 'GetCameraHeliocentricY',
    returnType: 'number',
    argTypes: [],
  ): () => number;
  (
    ident: 'GetCameraHeliocentricZ',
    returnType: 'number',
    argTypes: [],
  ): () => number;
  (
    ident: 'GetCameraYaw',
    returnType: 'number',
    argTypes: [],
  ): () => number;
  (
    ident: 'GetCameraPitch',
    returnType: 'number',
    argTypes: [],
  ): () => number;
  (
    ident: 'SetCameraRoll',
    returnType: null,
    argTypes: ['number'],
  ): (roll: number) => void;
  (
    ident: 'GetCameraRoll',
    returnType: 'number',
    argTypes: [],
  ): () => number;
  (
    ident: 'SetCameraFov',
    returnType: null,
    argTypes: ['number'],
  ): (fovDeg: number) => void;
  (
    ident: 'GetCameraFov',
    returnType: 'number',
    argTypes: [],
  ): () => number;
  (
    ident: 'AdvanceSimulationTo',
    returnType: null,
    argTypes: ['number'],
  ): (julianDate: number) => void;
  (
    ident: 'GetSceneStateJson',
    returnType: 'string',
    argTypes: [],
  ): () => string;
  (
    ident: 'GetBodyCatalogJson',
    returnType: 'string',
    argTypes: [],
  ): () => string;
  (
    ident: 'GetSurfaceJson',
    returnType: 'string',
    argTypes: ['number', 'number', 'number', 'number'],
  ): (bodyIndex: number, dx: number, dy: number, dz: number) => string;
  (
    ident: 'PreloadBodySystem',
    returnType: null,
    argTypes: ['number'],
  ): (bodyIndex: number) => void;
  (
    ident: 'GetPendingAssetCount',
    returnType: 'number',
    argTypes: [],
  ): () => number;
  (
    ident: 'SetHudOverlays',
    returnType: null,
    argTypes: ['number'],
  ): (mask: number) => void;
  (
    ident: 'GetHudOverlays',
    returnType: 'number',
    argTypes: [],
  ): () => number;
  (
    ident: 'BeginCapture',
    returnType: 'number',
    argTypes: ['number', 'number'],
  ): (width: number, height: number) => number;
  (
    ident: 'StepCaptureFrame',
    returnType: null,
    argTypes: ['number'],
  ): (deltaSeconds: number) => void;
  (
    ident: 'EndCapture',
    returnType: null,
    argTypes: [],
  ): () => void;
  (
    ident: 'GetShaderSourceFiles',
    returnType: 'string',
    argTypes: [],
  ): () => string;
  (
    ident: 'ReloadShaders',
    returnType: 'number',
    argTypes: [],
  ): () => number;
  // END GENERATED CWARP OVERLOADS
}

export type SettingsChangeField =
  | 'quality'
  | 'timeScale'
  | 'paused'
  | 'shadowQuality'
  | 'simulationEpoch'
  | 'orbitLines'
  | 'magneticFields'
  | 'magneticFieldMode'
  | 'observeMode'
  | 'observeSite'
  | 'observeTimeRate'
  | 'observeConstellations'
  | 'orbitScaleMode';

export interface SolarSystemModuleConfig {
  canvas: HTMLCanvasElement;
  locateFile?: (path: string, prefix: string) => string;
  print?: (text: string) => void;
  printErr?: (text: string) => void;
  onRuntimeInitialized?: () => void;
  /** C++ → JS callbacks registered on Module before runtime init. */
  /** `failed` (core resources that failed, already counted in `loaded`) is omitted by streaming progress. */
  updateLoadingProgress?: (loaded: number, total: number, failed?: number) => void;
  updateStreamingProgress?: (completed: number, total: number, active?: number, tierCode?: number) => void;
  onPlanetFocused?: (index: number) => void;
  /** 0–1 music gain for scene proximity; web/src/music.ts applies it. */
  onMusicDuck?: (duck: number) => void;
  onSettingsChanged?: (field: SettingsChangeField | string) => void;
  onNearestPlanetChanged?: (index: number) => void;
  onMissionFocused?: (index: number) => void;
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

/** The slice of Emscripten's FS the dev shader reload uses (EXPORTED_RUNTIME_METHODS 'FS'). */
export interface EmscriptenFS {
  readFile(path: string, opts: { encoding: 'utf8' }): string;
  writeFile(path: string, data: string | Uint8Array): void;
}

export interface SolarSystemModule {
  canvas: HTMLCanvasElement;
  FS?: EmscriptenFS;
  cwrap: SolarSystemCwrap;
  HEAPF32: Float32Array;
  HEAP8: Int8Array;
  GL?: EmscriptenGL;
  /** Emscripten main-loop controls (library_browser.js); used on WebGL context loss. */
  pauseMainLoop?: () => void;
  resumeMainLoop?: () => void;
  _main: (argc: number, argv: number) => number;
  // BEGIN GENERATED MODULE EXPORTS
  _SetCameraPose: (x: number, y: number, z: number, yaw: number, pitch: number) => void;
  _SetQualityPreset: (preset: number) => void;
  _GetQualityPreset: () => number;
  _SetTimeScale: (scale: number) => void;
  _GetTimeScale: () => number;
  _SetPaused: (paused: number) => void;
  _GetPaused: () => number;
  _SetSimulationEpoch: (julianDate: number) => void;
  _GetSimulationEpoch: () => number;
  _SetShadowQuality: (quality: number) => void;
  _GetShadowQuality: () => number;
  _SetTouchMovement: (forward: number, right: number, vertical: number) => void;
  _AddTouchLook: (deltaX: number, deltaY: number) => void;
  _AddTouchZoom: (delta: number) => void;
  _IsMobileWeb: () => number;
  _FocusPlanet: (idx: number) => void;
  _FocusMission: (idx: number) => void;
  _GetFocusedMissionIndex: () => number;
  _GetMissionCount: () => number;
  _GetMissionCatalogJson: () => number;
  _GetFocusedMissionJson: () => number;
  _SetOrbitScaleMode: (mode: number) => void;
  _GetOrbitScaleMode: () => number;
  _GetNearestPlanetIndex: () => number;
  _GetFocusedPlanetIndex: () => number;
  _GetPlanetSceneDistance: (idx: number) => number;
  _GetNextConjunctionJulianDate: () => number;
  _GetNextConjunctionBodyA: () => number;
  _GetNextConjunctionBodyB: () => number;
  _GetNextConjunctionSeparationDeg: () => number;
  _GetNextSkyEventJson: () => number;
  _SetObserveMode: (enabled: number) => void;
  _GetObserveMode: () => number;
  _SetObserverSite: (latDeg: number, lonDeg: number, altM: number) => void;
  _SetObserveView: (azimuthDeg: number, elevationDeg: number) => void;
  _SetObserveFov: (fovDeg: number) => void;
  _SetObserveTimeRate: (rate: number) => void;
  _SetObserveConstellations: (visible: number) => void;
  _GetObserveStateJson: () => number;
  _SetOrbitLines: (enabled: number) => void;
  _GetOrbitLines: () => number;
  _SetMagneticFields: (enabled: number) => void;
  _GetMagneticFields: () => number;
  _SetMagneticFieldMode: (enabled: number) => void;
  _GetMagneticFieldMode: () => number;
  _SetXrSessionActive: (active: number) => void;
  _SetXrBaseLayerFramebuffer: (framebuffer: number) => void;
  _SetXrEyeCount: (count: number) => void;
  _SetXrEyeViewport: (eye: number, x: number, y: number, width: number, height: number) => void;
  _GetXrMatrixScratch: () => number;
  _CommitXrEyeMatrices: (eye: number) => void;
  _RunXrFrame: () => void;
  _SetXrControllerRay: (hand: number, ox: number, oy: number, oz: number, dx: number, dy: number, dz: number, visible: number) => void;
  _GetCameraPositionX: () => number;
  _GetCameraPositionY: () => number;
  _GetCameraPositionZ: () => number;
  _GetCameraHeliocentricX: () => number;
  _GetCameraHeliocentricY: () => number;
  _GetCameraHeliocentricZ: () => number;
  _GetCameraYaw: () => number;
  _GetCameraPitch: () => number;
  _SetCameraRoll: (roll: number) => void;
  _GetCameraRoll: () => number;
  _SetCameraFov: (fovDeg: number) => void;
  _GetCameraFov: () => number;
  _AdvanceSimulationTo: (julianDate: number) => void;
  _GetSceneStateJson: () => number;
  _GetBodyCatalogJson: () => number;
  _GetSurfaceJson: (bodyIndex: number, dx: number, dy: number, dz: number) => number;
  _PreloadBodySystem: (bodyIndex: number) => void;
  _GetPendingAssetCount: () => number;
  _SetHudOverlays: (mask: number) => void;
  _GetHudOverlays: () => number;
  _BeginCapture: (width: number, height: number) => number;
  _StepCaptureFrame: (deltaSeconds: number) => void;
  _EndCapture: () => void;
  _GetShaderSourceFiles: () => number;
  _ReloadShaders: () => number;
  // END GENERATED MODULE EXPORTS
}

declare const SolarSystem: (
  config: SolarSystemModuleConfig,
) => Promise<SolarSystemModule>;

export default SolarSystem;

declare global {
  interface Window {
    /** Console helpers documented in AGENTS.md. */
    setCameraPose?: import('./wasmBridge').SolarSystemRuntime['setCameraPose'];
    setQualityPreset?: import('./wasmBridge').SolarSystemRuntime['setQualityPreset'];
    getQualityPreset?: import('./wasmBridge').SolarSystemRuntime['getQualityPreset'];
    getNextConjunction?: () => {
      valid: boolean;
      bodyA: number;
      bodyB: number;
      julianDate: number;
      separationDeg: number;
    };
    focusMission?: (index: number) => void;
    getFocusedMission?: () => {
      valid: boolean;
      id: string;
      index: number;
      x: number;
      y: number;
      z: number;
    };
    startTour?: (id?: string) => void;
    /** Cinematic timelines (timelineEditor.ts): the player, and load(id or URL) into the dock. */
    solarSystemTimeline?: {
      player: import('./timelinePlayer').TimelinePlayer;
      load: (source: string) => Promise<import('./timeline').TimelineParseResult>;
      snapshot: (t: number, width?: number, height?: number) => string | null;
    };
    /** Observe mode console helpers (see observe.ts / ObserveState). */
    setObserveMode?: (enabled: boolean) => void;
    setObserverSite?: (latDeg: number, lonDeg: number, altM?: number) => void;
    getObserveState?: () => import('./observe').ObserveState;
    /** Dev server only (shaderReload.ts): rebuild the programs whose shader files changed. */
    __solarSystem?: {
      reloadShaders?: () => Promise<number>;
    };
    /** Runtime asset base URL for WebResourceFetcher. */
    __solarSystemAssetBase?: string;
    /**
     * Download hook for WebResourceFetcher: resolves the body, or the HTTP status of a
     * failure (0 = network). Installed by bootstrap.ts (assetCache.ts); when absent C++
     * falls back to emscripten_async_wget2 with no cache.
     */
    __solarSystemFetchAsset?: (url: string) => Promise<Uint8Array<ArrayBuffer> | number>;
    /** KTX2 texture packs published by this deployment — read by PlatformWindow.cpp. */
    __solarSystemTexturePacks?: string[];
    /** Init config published before Module() — read by QualitySettings.cpp. */
    __solarSystemInit?: {
      qualityPreset: QualityPreset;
      isMobileWeb: boolean;
      backingStoreScale: number;
    };
  }
}
