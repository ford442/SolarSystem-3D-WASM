/**
 * Which Wasm binary to start. The pthread build (SolarSystem-mt.js/.wasm, CMake
 * SOLARSYSTEM_WEB_THREADS) runs the C++ job pool (core/JobSystem.h) on Web Workers, but a
 * worker can only receive the shared Wasm memory on a cross-origin isolated page (COEP
 * require-corp + COOP same-origin, docs/CROSS_ORIGIN_HEADERS.md). Everywhere else, and
 * under `?jobs=0`, the single-threaded SolarSystem.js runs the same jobs inline.
 */
export type WasmVariant = 'threaded' | 'single';

export interface VariantInputs {
    crossOriginIsolated: boolean;
    search: string;
}

/** `?jobs=0` asks for no worker threads (C++ reads the same parameter). */
export function jobsDisabled(search: string): boolean {
    return new URLSearchParams(search).get('jobs') === '0';
}

export function chooseWasmVariant({ crossOriginIsolated, search }: VariantInputs): WasmVariant {
    return crossOriginIsolated && !jobsDisabled(search) ? 'threaded' : 'single';
}

/** The threaded glue sits in public/ (Vite must not bundle it: its workers re-import it by URL). */
export const THREADED_GLUE = 'SolarSystem-mt.js';

/**
 * locateFile for either glue: binaries resolve against the deployed base, and both variants
 * share one SolarSystem.data (the preload list is the same; build-web.sh checks the bytes).
 */
export function locateWasmFile(path: string, prefix: string, deployedBase: URL): string {
    if (path.endsWith('.data')) {
        return new URL(path.replace(/-mt\.data$/, '.data'), deployedBase).toString();
    }
    if (path.endsWith('.wasm')) {
        return new URL(path, deployedBase).toString();
    }
    return prefix + path;
}
