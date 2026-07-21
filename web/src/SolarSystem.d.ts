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
/** Returns 1 while paused and 0 while running. */
export type GetPaused = () => number;

/** Set the simulation epoch as a Julian Date (fractional day allowed). */
export type SetSimulationEpoch = (julianDate: number) => void;
/** Read the active simulation Julian Date from the C++ runtime. */
export type GetSimulationEpoch = () => number;

export type ShadowQuality = 0 | 1 | 2 | 3;
/** Set shadow quality: 0=off, 1=low, 2=medium, 3=full. */
export type SetShadowQuality = (quality: ShadowQuality) => void;
export type GetShadowQuality = () => ShadowQuality;

export type PlanetIndex = 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9;

export type OrbitScaleMode = 0 | 1;

export type SetMusicVolume = (volume: number) => void;
export type GetMusicVolume = () => number;
export type SetMusicMuted = (muted: boolean) => void;
export type GetMusicMuted = () => number;

/** Planet index accepted by FocusPlanet: 0=sun, 1=Mercury, ..., 9=Pluto. */
export type FocusPlanet = (index: PlanetIndex) => void;

/** 0=compressed artistic orbits, 1=AU-proportional spacing. */
export type SetOrbitScaleMode = (mode: OrbitScaleMode) => void;
export type GetOrbitScaleMode = () => OrbitScaleMode;
/** Returns 1–9 for nearest loaded planet, or -1 when unknown. */
export type GetNearestPlanetIndex = () => number;
export type GetFocusedPlanetIndex = () => number;
export type GetPlanetSceneDistance = (index: PlanetIndex) => number;

/** Toggle faint heliocentric orbit path lines (1=on, 0=off). */
export type SetOrbitLines = (enabled: number) => void;
export type GetOrbitLines = () => number;

export type SetXrSessionActive = (active: number) => void;
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

export interface SolarSystemCwrap {
  (
    ident: 'SetCameraPose',
    returnType: null,
    argTypes: ['number', 'number', 'number', 'number', 'number'],
  ): SetCameraPose;
  (
    ident: 'SetQualityPreset',
    returnType: null,
    argTypes: ['number'],
  ): SetQualityPreset;
  (
    ident: 'GetQualityPreset',
    returnType: 'number',
    argTypes: [],
  ): GetQualityPreset;
  (
    ident: 'SetTimeScale',
    returnType: null,
    argTypes: ['number'],
  ): SetTimeScale;
  (
    ident: 'GetTimeScale',
    returnType: 'number',
    argTypes: [],
  ): GetTimeScale;
  (
    ident: 'SetPaused',
    returnType: null,
    argTypes: ['number'],
  ): SetPaused;
  (
    ident: 'GetPaused',
    returnType: 'number',
    argTypes: [],
  ): GetPaused;
  (
    ident: 'SetSimulationEpoch',
    returnType: null,
    argTypes: ['number'],
  ): SetSimulationEpoch;
  (
    ident: 'GetSimulationEpoch',
    returnType: 'number',
    argTypes: [],
  ): GetSimulationEpoch;
  (
    ident: 'SetShadowQuality',
    returnType: null,
    argTypes: ['number'],
  ): SetShadowQuality;
  (
    ident: 'GetShadowQuality',
    returnType: 'number',
    argTypes: [],
  ): GetShadowQuality;
  (
    ident: 'SetTouchMovement',
    returnType: null,
    argTypes: ['number', 'number', 'number'],
  ): SetTouchMovement;
  (
    ident: 'AddTouchLook',
    returnType: null,
    argTypes: ['number', 'number'],
  ): AddTouchLook;
  (
    ident: 'AddTouchZoom',
    returnType: null,
    argTypes: ['number'],
  ): AddTouchZoom;
  (
    ident: 'IsMobileWeb',
    returnType: 'number',
    argTypes: [],
  ): () => number;
  (
    ident: 'SetMusicVolume',
    returnType: null,
    argTypes: ['number'],
  ): SetMusicVolume;
  (
    ident: 'GetMusicVolume',
    returnType: 'number',
    argTypes: [],
  ): GetMusicVolume;
  (
    ident: 'SetMusicMuted',
    returnType: null,
    argTypes: ['number'],
  ): SetMusicMuted;
  (
    ident: 'GetMusicMuted',
    returnType: 'number',
    argTypes: [],
  ): GetMusicMuted;
  (
    ident: 'FocusPlanet',
    returnType: null,
    argTypes: ['number'],
  ): FocusPlanet;
  (
    ident: 'SetOrbitScaleMode',
    returnType: null,
    argTypes: ['number'],
  ): SetOrbitScaleMode;
  (
    ident: 'GetOrbitScaleMode',
    returnType: 'number',
    argTypes: [],
  ): GetOrbitScaleMode;
  (
    ident: 'GetNearestPlanetIndex',
    returnType: 'number',
    argTypes: [],
  ): GetNearestPlanetIndex;
  (
    ident: 'GetFocusedPlanetIndex',
    returnType: 'number',
    argTypes: [],
  ): GetFocusedPlanetIndex;
  (
    ident: 'GetPlanetSceneDistance',
    returnType: 'number',
    argTypes: ['number'],
  ): GetPlanetSceneDistance;
  (
    ident: 'SetOrbitLines',
    returnType: null,
    argTypes: ['number'],
  ): SetOrbitLines;
  (
    ident: 'GetOrbitLines',
    returnType: 'number',
    argTypes: [],
  ): GetOrbitLines;
  (
    ident: 'SetXrSessionActive',
    returnType: null,
    argTypes: ['number'],
  ): SetXrSessionActive;
  (
    ident: 'SetXrEyeCount',
    returnType: null,
    argTypes: ['number'],
  ): SetXrEyeCount;
  (
    ident: 'SetXrEyeViewport',
    returnType: null,
    argTypes: ['number', 'number', 'number', 'number', 'number'],
  ): SetXrEyeViewport;
  (
    ident: 'GetXrMatrixScratch',
    returnType: 'number',
    argTypes: [],
  ): GetXrMatrixScratch;
  (
    ident: 'CommitXrEyeMatrices',
    returnType: null,
    argTypes: ['number'],
  ): CommitXrEyeMatrices;
  (
    ident: 'RunXrFrame',
    returnType: null,
    argTypes: [],
  ): RunXrFrame;
  (
    ident: 'GetCameraPositionX',
    returnType: 'number',
    argTypes: [],
  ): GetCameraPositionComponent;
  (
    ident: 'GetCameraPositionY',
    returnType: 'number',
    argTypes: [],
  ): GetCameraPositionComponent;
  (
    ident: 'GetCameraPositionZ',
    returnType: 'number',
    argTypes: [],
  ): GetCameraPositionComponent;
}

export interface SolarSystemModuleConfig {
  canvas: HTMLCanvasElement;
  locateFile?: (path: string, prefix: string) => string;
  print?: (text: string) => void;
  printErr?: (text: string) => void;
  onRuntimeInitialized?: () => void;
  /** Passed through to Emscripten's WebGL context creation (GLFW). */
  contextAttributes?: {
    xrCompatible?: boolean;
    majorVersion?: number;
    minorVersion?: number;
    antialias?: boolean;
  };
}

export interface SolarSystemModule {
  canvas: HTMLCanvasElement;
  cwrap: SolarSystemCwrap;
  HEAPF32: Float32Array;
  _main: (argc: number, argv: number) => number;
  _SetCameraPose: SetCameraPose;
  _SetQualityPreset: SetQualityPreset;
  _GetQualityPreset: GetQualityPreset;
  _SetTimeScale: SetTimeScale;
  _GetTimeScale: GetTimeScale;
  _SetPaused: SetPaused;
  _GetPaused: GetPaused;
  _SetSimulationEpoch: SetSimulationEpoch;
  _GetSimulationEpoch: GetSimulationEpoch;
  _SetShadowQuality: SetShadowQuality;
  _GetShadowQuality: GetShadowQuality;
  _SetTouchMovement: SetTouchMovement;
  _AddTouchLook: AddTouchLook;
  _AddTouchZoom: AddTouchZoom;
  _IsMobileWeb: () => number;
  _SetMusicVolume: SetMusicVolume;
  _GetMusicVolume: GetMusicVolume;
  _SetMusicMuted: SetMusicMuted;
  _GetMusicMuted: GetMusicMuted;
  _FocusPlanet: FocusPlanet;
  _SetOrbitScaleMode: SetOrbitScaleMode;
  _GetOrbitScaleMode: GetOrbitScaleMode;
  _GetNearestPlanetIndex: GetNearestPlanetIndex;
  _GetFocusedPlanetIndex: GetFocusedPlanetIndex;
  _GetPlanetSceneDistance: GetPlanetSceneDistance;
  _SetOrbitLines: SetOrbitLines;
  _GetOrbitLines: GetOrbitLines;
}

declare const SolarSystem: (
  config: SolarSystemModuleConfig,
) => Promise<SolarSystemModule>;

export default SolarSystem;

declare global {
  interface Window {
    updateLoadingProgress?: (loaded: number, total: number) => void;
    updateStreamingProgress?: (completed: number, total: number, active?: number) => void;
    setCameraPose?: SetCameraPose;
    setQualityPreset?: SetQualityPreset;
    getQualityPreset?: GetQualityPreset;
    setTimeScale?: SetTimeScale;
    getTimeScale?: GetTimeScale;
    setPaused?: SetPaused;
    getPaused?: GetPaused;
    setSimulationEpoch?: SetSimulationEpoch;
    getSimulationEpoch?: GetSimulationEpoch;
    setShadowQuality?: SetShadowQuality;
    getShadowQuality?: GetShadowQuality;
    setTouchMovement?: SetTouchMovement;
    addTouchLook?: AddTouchLook;
    addTouchZoom?: AddTouchZoom;
    isMobileWeb?: () => number;
    setMusicVolume?: SetMusicVolume;
    getMusicVolume?: GetMusicVolume;
    setMusicMuted?: SetMusicMuted;
    getMusicMuted?: GetMusicMuted;
    focusPlanet?: FocusPlanet;
    setOrbitScaleMode?: SetOrbitScaleMode;
    getOrbitScaleMode?: GetOrbitScaleMode;
    getNearestPlanetIndex?: GetNearestPlanetIndex;
    getFocusedPlanetIndex?: GetFocusedPlanetIndex;
    getPlanetSceneDistance?: GetPlanetSceneDistance;
    setOrbitLines?: SetOrbitLines;
    getOrbitLines?: GetOrbitLines;
    setXrSessionActive?: SetXrSessionActive;
    setXrEyeCount?: SetXrEyeCount;
    setXrEyeViewport?: SetXrEyeViewport;
    getXrMatrixScratch?: GetXrMatrixScratch;
    commitXrEyeMatrices?: CommitXrEyeMatrices;
    runXrFrame?: RunXrFrame;
    getCameraPositionX?: GetCameraPositionComponent;
    getCameraPositionY?: GetCameraPositionComponent;
    getCameraPositionZ?: GetCameraPositionComponent;
    onPlanetFocused?: (index: number) => void;
    __solarSystemAssetBase?: string;
  }
}
