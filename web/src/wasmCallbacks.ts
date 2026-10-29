import type { SettingsChangeField } from './SolarSystem.js';

export interface WasmModuleCallbacks {
    /** `failed` counts core resources that failed to download; they are already included in `loaded`. */
    updateLoadingProgress?: (loaded: number, total: number, failed?: number) => void;
    updateStreamingProgress?: (completed: number, total: number, active?: number, tierCode?: number) => void;
    onPlanetFocused?: (index: number) => void;
    /** 0–1 gain for scene proximity, reported by C++ (Application::UpdateMusicDucking). */
    onMusicDuck?: (duck: number) => void;
    onSettingsChanged?: (field: SettingsChangeField | string) => void;
    /** JS planet index (GetNearestPlanetIndex convention) whenever the nearest body changes; -1 for none. */
    onNearestPlanetChanged?: (index: number) => void;
    /** Mission index whenever mission follow starts or stops (-1 when it stops). */
    onMissionFocused?: (index: number) => void;
}

type CallbackKey = keyof WasmModuleCallbacks;
type AnyCallback = (...args: never[]) => void;
type MutableModule = Record<string, unknown>;

// The build is MODULARIZE + EXPORT_ES6, so there is no global `Module`: JsBridge.cpp's
// `Module['onPlanetFocused']` reads the object handed to the factory, which is the same
// object the factory's promise resolves to. Every key below is installed on that object as
// a dispatcher that fans out to all listeners, so several panels can share one C++ event
// and listeners registered before the instance resolves are not lost.
const listeners = new Map<CallbackKey, Set<AnyCallback>>();
const dispatchers = new Map<CallbackKey, AnyCallback>();
const targets = new Set<MutableModule>();

function dispatcherFor(key: CallbackKey): AnyCallback {
    let dispatcher = dispatchers.get(key);
    if (!dispatcher) {
        dispatcher = (...args: never[]) => {
            for (const listener of [...(listeners.get(key) ?? [])]) {
                try {
                    listener(...args);
                } catch (error) {
                    console.error(`[wasmCallbacks] ${key} listener threw:`, error);
                }
            }
        };
        dispatchers.set(key, dispatcher);
    }
    return dispatcher;
}

function installOn(target: MutableModule, key: CallbackKey): void {
    target[key] = dispatcherFor(key);
}

function addListeners(callbacks: WasmModuleCallbacks): void {
    for (const [key, value] of Object.entries(callbacks) as [CallbackKey, AnyCallback | undefined][]) {
        if (value === undefined) continue;
        let set = listeners.get(key);
        if (!set) {
            set = new Set();
            listeners.set(key, set);
        }
        set.add(value);
        for (const target of targets) {
            installOn(target, key);
        }
    }
}

/** Point every registered (and future) callback at the resolved module instance. */
export function setModuleInstance(instance: object): void {
    const target = instance as MutableModule;
    targets.add(target);
    for (const key of listeners.keys()) {
        installOn(target, key);
    }
}

export function registerWasmCallbacks(callbacks: WasmModuleCallbacks): void {
    addListeners(callbacks);
}

export function clearWasmCallback(key: CallbackKey, expected: unknown): void {
    listeners.get(key)?.delete(expected as AnyCallback);
}

/**
 * Callbacks C++ may call while main() is still running (loading progress, music duck),
 * before the factory promise resolves: they go onto the config object itself, which is
 * the object the module instance is built on.
 */
export function installWasmCallbacksOnConfig<T extends Record<string, unknown>>(
    config: T,
    callbacks: WasmModuleCallbacks,
): T & WasmModuleCallbacks {
    addListeners(callbacks);
    const target = { ...config } as MutableModule;
    setModuleInstance(target);
    return target as T & WasmModuleCallbacks;
}

/**
 * Deliver an event from the JS side to the same listeners C++ reaches, for state that only
 * JS changes (orbit scale mode, mission focus requested from a panel or tour).
 */
export function emitWasmCallback<K extends CallbackKey>(
    key: K,
    ...args: Parameters<NonNullable<WasmModuleCallbacks[K]>>
): void {
    (dispatcherFor(key) as (...a: unknown[]) => void)(...args);
}
