import type {
    OrbitScaleMode,
    PlanetIndex,
    QualityPreset,
    ShadowQuality,
    SolarSystemModule,
} from './SolarSystem.js';

/**
 * Planet index conventions (FocusPlanet / GetFocusedPlanetIndex):
 * - 0 = Sun, 1 = Mercury … 9 = Pluto (matches deepLink PLANET_IDS).
 * GetNearestPlanetIndex returns 1–9 for the nearest loaded planet, or -1 when unknown
 * (C++ adds +1 offset via GetNearestPlanetIndexForJs).
 */
export type { PlanetIndex, QualityPreset, ShadowQuality, OrbitScaleMode };

/** Map quality preset + shadows toggle to C++ shadow quality (0=off, 1–3 = low…full). */
export function shadowQualityForPreset(preset: QualityPreset, shadowsEnabled: boolean): ShadowQuality {
    return shadowsEnabled ? (preset + 1) as ShadowQuality : 0;
}

export type SettingsChangeField =
    | 'quality'
    | 'timeScale'
    | 'paused'
    | 'shadowQuality'
    | 'musicVolume'
    | 'musicMuted'
    | 'simulationEpoch'
    | 'orbitLines';

export interface CameraPose {
    x: number;
    y: number;
    z: number;
    yaw: number;
    pitch: number;
}

/** Typed façade over all EMSCRIPTEN_KEEPALIVE cwrap exports. */
export interface SolarSystemRuntime {
    readonly heapF32: Float32Array;

    setCameraPose(x: number, y: number, z: number, yaw: number, pitch: number): void;
    setQualityPreset(preset: QualityPreset): void;
    getQualityPreset(): QualityPreset;
    setTimeScale(scale: number): void;
    getTimeScale(): number;
    setPaused(paused: boolean): void;
    getPaused(): boolean;
    setSimulationEpoch(julianDate: number): void;
    getSimulationEpoch(): number;
    setShadowQuality(quality: ShadowQuality): void;
    getShadowQuality(): ShadowQuality;
    setTouchMovement(forward: number, right: number, vertical: number): void;
    addTouchLook(deltaX: number, deltaY: number): void;
    addTouchZoom(delta: number): void;
    isMobileWeb(): boolean;
    setMusicVolume(volume: number): void;
    getMusicVolume(): number;
    setMusicMuted(muted: boolean): void;
    getMusicMuted(): boolean;
    focusPlanet(index: PlanetIndex): void;
    setOrbitScaleMode(mode: OrbitScaleMode): void;
    getOrbitScaleMode(): OrbitScaleMode;
    /** 1–9 nearest loaded planet, or -1. See planet index note above. */
    getNearestPlanetIndex(): number;
    getFocusedPlanetIndex(): number;
    getPlanetSceneDistance(index: PlanetIndex): number;
    setOrbitLines(enabled: boolean): void;
    getOrbitLines(): boolean;
    setXrSessionActive(active: boolean): void;
    setXrEyeCount(count: number): void;
    setXrEyeViewport(eye: number, x: number, y: number, width: number, height: number): void;
    getXrMatrixScratchPtr(): number;
    commitXrEyeMatrices(eye: number): void;
    runXrFrame(): void;
    getCameraPosition(): { x: number; y: number; z: number };
    getCameraYaw(): number;
    getCameraPitch(): number;
}

let activeRuntime: SolarSystemRuntime | null = null;

export function getSolarSystemRuntime(): SolarSystemRuntime | null {
    return activeRuntime;
}

export function createSolarSystemRuntime(instance: SolarSystemModule): SolarSystemRuntime {
    const cwrap = instance.cwrap.bind(instance);

    const runtime: SolarSystemRuntime = {
        heapF32: instance.HEAPF32,

        setCameraPose: cwrap('SetCameraPose', null, ['number', 'number', 'number', 'number', 'number']),
        setQualityPreset: (preset) => cwrap('SetQualityPreset', null, ['number'])(preset),
        getQualityPreset: () => cwrap('GetQualityPreset', 'number', [])() as QualityPreset,
        setTimeScale: cwrap('SetTimeScale', null, ['number']),
        getTimeScale: cwrap('GetTimeScale', 'number', []),
        setPaused: (paused) => cwrap('SetPaused', null, ['number'])(paused ? 1 : 0),
        getPaused: () => cwrap('GetPaused', 'number', [])() !== 0,
        setSimulationEpoch: cwrap('SetSimulationEpoch', null, ['number']),
        getSimulationEpoch: cwrap('GetSimulationEpoch', 'number', []),
        setShadowQuality: (quality) => cwrap('SetShadowQuality', null, ['number'])(quality),
        getShadowQuality: () => cwrap('GetShadowQuality', 'number', [])() as ShadowQuality,
        setTouchMovement: cwrap('SetTouchMovement', null, ['number', 'number', 'number']),
        addTouchLook: cwrap('AddTouchLook', null, ['number', 'number']),
        addTouchZoom: cwrap('AddTouchZoom', null, ['number']),
        isMobileWeb: () => cwrap('IsMobileWeb', 'number', [])() !== 0,
        setMusicVolume: cwrap('SetMusicVolume', null, ['number']),
        getMusicVolume: cwrap('GetMusicVolume', 'number', []),
        setMusicMuted: (muted) => cwrap('SetMusicMuted', null, ['number'])(muted ? 1 : 0),
        getMusicMuted: () => cwrap('GetMusicMuted', 'number', [])() !== 0,
        focusPlanet: (index) => cwrap('FocusPlanet', null, ['number'])(index),
        setOrbitScaleMode: (mode) => cwrap('SetOrbitScaleMode', null, ['number'])(mode),
        getOrbitScaleMode: () => cwrap('GetOrbitScaleMode', 'number', [])() as OrbitScaleMode,
        getNearestPlanetIndex: cwrap('GetNearestPlanetIndex', 'number', []),
        getFocusedPlanetIndex: cwrap('GetFocusedPlanetIndex', 'number', []),
        getPlanetSceneDistance: cwrap('GetPlanetSceneDistance', 'number', ['number']),
        setOrbitLines: (enabled) => cwrap('SetOrbitLines', null, ['number'])(enabled ? 1 : 0),
        getOrbitLines: () => cwrap('GetOrbitLines', 'number', [])() !== 0,
        setXrSessionActive: (active) => cwrap('SetXrSessionActive', null, ['number'])(active ? 1 : 0),
        setXrEyeCount: cwrap('SetXrEyeCount', null, ['number']),
        setXrEyeViewport: cwrap('SetXrEyeViewport', null, ['number', 'number', 'number', 'number', 'number']),
        getXrMatrixScratchPtr: cwrap('GetXrMatrixScratch', 'number', []),
        commitXrEyeMatrices: cwrap('CommitXrEyeMatrices', null, ['number']),
        runXrFrame: cwrap('RunXrFrame', null, []),
        getCameraPosition: () => ({
            x: cwrap('GetCameraPositionX', 'number', [])(),
            y: cwrap('GetCameraPositionY', 'number', [])(),
            z: cwrap('GetCameraPositionZ', 'number', [])(),
        }),
        getCameraYaw: cwrap('GetCameraYaw', 'number', []),
        getCameraPitch: cwrap('GetCameraPitch', 'number', []),
    };

    activeRuntime = runtime;
    return runtime;
}

/** Console helper documented in AGENTS.md — thin alias over the typed bridge. */
export function exposeConsoleHelpers(runtime: SolarSystemRuntime): void {
    window.setCameraPose = runtime.setCameraPose.bind(runtime);
}

export function subscribeSettingsChanges(
    handler: (field: SettingsChangeField) => void,
): () => void {
    const wrapped = (field: string) => handler(field as SettingsChangeField);
    window.onSettingsChanged = wrapped;
    return () => {
        if (window.onSettingsChanged === wrapped) {
            window.onSettingsChanged = undefined;
        }
    };
}
