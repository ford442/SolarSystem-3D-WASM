import type {
    OrbitScaleMode,
    PlanetIndex,
    QualityPreset,
    ShadowQuality,
    SolarSystemModule,
} from './SolarSystem.js';
import { createCachedCwrapExports } from './wasmBridge.exports.js';
import { clearWasmCallback, registerWasmCallbacks } from './wasmCallbacks.js';

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
    | 'orbitLines'
    | 'magneticFields'
    | 'magneticFieldMode';

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
    setMagneticFields(enabled: boolean): void;
    getMagneticFields(): boolean;
    setMagneticFieldMode(enabled: boolean): void;
    getMagneticFieldMode(): boolean;
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
    const exports = createCachedCwrapExports(instance.cwrap);

    const runtime: SolarSystemRuntime = {
        heapF32: instance.HEAPF32,

        setCameraPose: exports.setCameraPose,
        setQualityPreset: (preset) => exports.setQualityPreset(preset),
        getQualityPreset: () => exports.getQualityPreset() as QualityPreset,
        setTimeScale: exports.setTimeScale,
        getTimeScale: exports.getTimeScale,
        setPaused: (paused) => exports.setPaused(paused ? 1 : 0),
        getPaused: () => exports.getPaused() !== 0,
        setSimulationEpoch: exports.setSimulationEpoch,
        getSimulationEpoch: exports.getSimulationEpoch,
        setShadowQuality: (quality) => exports.setShadowQuality(quality),
        getShadowQuality: () => exports.getShadowQuality() as ShadowQuality,
        setTouchMovement: exports.setTouchMovement,
        addTouchLook: exports.addTouchLook,
        addTouchZoom: exports.addTouchZoom,
        isMobileWeb: () => exports.isMobileWeb() !== 0,
        setMusicVolume: exports.setMusicVolume,
        getMusicVolume: exports.getMusicVolume,
        setMusicMuted: (muted) => exports.setMusicMuted(muted ? 1 : 0),
        getMusicMuted: () => exports.getMusicMuted() !== 0,
        focusPlanet: (index) => exports.focusPlanet(index),
        setOrbitScaleMode: (mode) => exports.setOrbitScaleMode(mode),
        getOrbitScaleMode: () => exports.getOrbitScaleMode() as OrbitScaleMode,
        getNearestPlanetIndex: exports.getNearestPlanetIndex,
        getFocusedPlanetIndex: exports.getFocusedPlanetIndex,
        getPlanetSceneDistance: (index) => exports.getPlanetSceneDistance(index),
        setOrbitLines: (enabled) => exports.setOrbitLines(enabled ? 1 : 0),
        getOrbitLines: () => exports.getOrbitLines() !== 0,
        setMagneticFields: (enabled) => exports.setMagneticFields(enabled ? 1 : 0),
        getMagneticFields: () => exports.getMagneticFields() !== 0,
        setMagneticFieldMode: (enabled) => exports.setMagneticFieldMode(enabled ? 1 : 0),
        getMagneticFieldMode: () => exports.getMagneticFieldMode() !== 0,
        setXrSessionActive: (active) => exports.setXrSessionActive(active ? 1 : 0),
        setXrEyeCount: exports.setXrEyeCount,
        setXrEyeViewport: exports.setXrEyeViewport,
        getXrMatrixScratchPtr: exports.getXrMatrixScratch,
        commitXrEyeMatrices: exports.commitXrEyeMatrices,
        runXrFrame: exports.runXrFrame,
        getCameraPosition: () => ({
            x: exports.getCameraPositionX(),
            y: exports.getCameraPositionY(),
            z: exports.getCameraPositionZ(),
        }),
        getCameraYaw: exports.getCameraYaw,
        getCameraPitch: exports.getCameraPitch,
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
    registerWasmCallbacks({ onSettingsChanged: wrapped });
    return () => {
        clearWasmCallback('onSettingsChanged', wrapped);
    };
}
