import type { SettingsChangeField } from './wasmBridge.js';

export interface WasmModuleCallbacks {
    updateLoadingProgress?: (loaded: number, total: number) => void;
    updateStreamingProgress?: (completed: number, total: number, active?: number, tierCode?: number) => void;
    onPlanetFocused?: (index: number) => void;
    onSettingsChanged?: (field: SettingsChangeField | string) => void;
}

type MutableModule = Record<string, unknown>;

function getModuleObject(): MutableModule {
    return (globalThis as { Module?: MutableModule }).Module ?? {};
}

export function registerWasmCallbacks(callbacks: WasmModuleCallbacks): void {
    const moduleObject = getModuleObject();
    for (const [key, value] of Object.entries(callbacks)) {
        if (value !== undefined) {
            moduleObject[key] = value;
        }
    }
}

export function clearWasmCallback(key: keyof WasmModuleCallbacks, expected: unknown): void {
    const moduleObject = getModuleObject();
    if (moduleObject[key] === expected) {
        delete moduleObject[key];
    }
}

export function installWasmCallbacksOnConfig<T extends Record<string, unknown>>(
    config: T,
    callbacks: WasmModuleCallbacks,
): T & WasmModuleCallbacks {
    return { ...config, ...callbacks };
}
