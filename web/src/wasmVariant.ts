/**
 * Which Wasm binary to start: {webgl2, webgpu} × {single, threaded}.
 *
 * Threads: the pthread build (SolarSystem-mt.js/.wasm, CMake SOLARSYSTEM_WEB_THREADS) runs the
 * C++ job pool (core/JobSystem.h) on Web Workers, but a worker can only receive the shared Wasm
 * memory on a cross-origin isolated page (COEP require-corp + COOP same-origin,
 * docs/CROSS_ORIGIN_HEADERS.md). Everywhere else, and under `?jobs=0`, the single-threaded build
 * runs the same jobs inline.
 *
 * Backend: the WebGPU build (SolarSystem-wgpu[-mt], CMake SOLARSYSTEM_WEBGPU, the RHI's second
 * backend: docs/plans/WEBGPU_BACKEND_PLAN.md) is loaded only when the page asks for it with
 * `?gpu=webgpu`, the browser has navigator.gpu, and the deployment publishes the binary
 * (VITE_WEBGPU_BUILD=1). Left to itself (`auto`, or `?gpu=webgl`) the page stays on WebGL 2:
 * until the WebGPU backend draws the whole frame, WEBGPU_AUTO_SELECT stays false.
 */
export type WasmVariant = 'threaded' | 'single';
export type GpuBackend = 'webgl2' | 'webgpu';
export type GpuRequest = 'auto' | 'webgl' | 'webgpu';

/** Flip once WebGPU renders every pass at golden parity: `auto` then prefers it where available. */
export const WEBGPU_AUTO_SELECT = false;

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

/** `?gpu=webgpu` / `?gpu=webgl` (`webgl2`, `gl` too); anything else is `auto`. */
export function parseGpuRequest(search: string): GpuRequest {
    const value = new URLSearchParams(search).get('gpu')?.trim().toLowerCase();
    if (value === 'webgpu' || value === 'wgpu') {
        return 'webgpu';
    }
    if (value === 'webgl' || value === 'webgl2' || value === 'gl') {
        return 'webgl';
    }
    return 'auto';
}

export interface BackendInputs {
    search: string;
    /** navigator.gpu exists (the adapter is only requested once WebGPU is chosen). */
    webgpuAvailable: boolean;
    /** The deployment ships SolarSystem-wgpu.js (VITE_WEBGPU_BUILD=1). */
    webgpuPublished: boolean;
    autoSelect?: boolean;
}

export interface BackendChoice {
    backend: GpuBackend;
    /** Why, for the console: "?gpu=webgpu", "auto: WebGL 2 until …", "no navigator.gpu". */
    reason: string;
}

export function chooseGpuBackend({
    search,
    webgpuAvailable,
    webgpuPublished,
    autoSelect = WEBGPU_AUTO_SELECT,
}: BackendInputs): BackendChoice {
    const request = parseGpuRequest(search);
    if (request === 'webgl') {
        return { backend: 'webgl2', reason: '?gpu=webgl' };
    }
    if (request === 'auto' && !autoSelect) {
        return { backend: 'webgl2', reason: 'auto: WebGL 2 until the WebGPU backend draws the whole frame' };
    }
    if (!webgpuPublished) {
        return { backend: 'webgl2', reason: 'this deployment has no WebGPU build' };
    }
    if (!webgpuAvailable) {
        return { backend: 'webgl2', reason: 'no navigator.gpu in this browser' };
    }
    return { backend: 'webgpu', reason: request === 'webgpu' ? '?gpu=webgpu' : 'auto' };
}

/** The threaded glue sits in public/ (Vite must not bundle it: its workers re-import it by URL). */
export const THREADED_GLUE = 'SolarSystem-mt.js';

/**
 * The glue to import for a binary, from public/ by URL; null is the single-threaded WebGL 2
 * build Vite bundles (src/SolarSystem.js).
 */
export function wasmGlue(backend: GpuBackend, threads: WasmVariant): string | null {
    if (backend === 'webgpu') {
        return threads === 'threaded' ? 'SolarSystem-wgpu-mt.js' : 'SolarSystem-wgpu.js';
    }
    return threads === 'threaded' ? THREADED_GLUE : null;
}

/**
 * locateFile for any glue: binaries resolve against the deployed base. The two threading
 * variants of a backend share one .data (the preload list is the same; build-web.sh checks the
 * bytes): SolarSystem.data for WebGL 2, SolarSystem-wgpu.data (which adds the WGSL) for WebGPU.
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
