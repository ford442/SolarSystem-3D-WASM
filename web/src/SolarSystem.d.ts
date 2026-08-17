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
/** Toggle magnetic field ribbon overlay (1=on, 0=off). */
export type SetMagneticFields = (enabled: number) => void;
export type GetMagneticFields = () => number;

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
    ident: 'SetXrSessionActive',
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
  _SetMagneticFields: SetMagneticFields;
  _GetMagneticFields: GetMagneticFields;
}

declare const SolarSystem: (
  config: SolarSystemModuleConfig,
) => Promise<SolarSystemModule>;

export default SolarSystem;

declare global {
  interface Window {
    /** C++ loading progress callback (EM_ASM). */
    updateLoadingProgress?: (loaded: number, total: number) => void;
    /** C++ LOD streaming progress callback (EM_ASM). */
    /** tierCode: 0=generic, 1=mid, 2=high */
    updateStreamingProgress?: (completed: number, total: number, active?: number, tierCode?: number) => void;
    /** Console helper documented in AGENTS.md. */
    setCameraPose?: SetCameraPose;
    /** C++ planet focus callback (EM_ASM). */
    onPlanetFocused?: (index: number) => void;
    /** C++ settings change callback (keyboard shortcuts / runtime toggles). */
    onSettingsChanged?: (field:
      | 'quality'
      | 'timeScale'
      | 'paused'
      | 'shadowQuality'
      | 'musicVolume'
      | 'musicMuted'
      | 'simulationEpoch'
      | 'orbitLines'
      | 'magneticFields'
      | string) => void;
    /** Runtime asset base URL for WebResourceFetcher. */
    __solarSystemAssetBase?: string;
    /** Init config published before Module() — read by QualitySettings.cpp. */
    __solarSystemInit?: {
      qualityPreset: QualityPreset;
      isMobileWeb: boolean;
    };
  }
}
