import { describe, expect, it } from 'vitest';
import {
    chooseGpuBackend,
    chooseWasmVariant,
    jobsDisabled,
    locateWasmFile,
    parseGpuRequest,
    wasmGlue,
} from './wasmVariant';

describe('chooseWasmVariant', () => {
    it('loads the threaded build only on an isolated page that did not ask for ?jobs=0', () => {
        expect(chooseWasmVariant({ crossOriginIsolated: true, search: '' })).toBe('threaded');
        expect(chooseWasmVariant({ crossOriginIsolated: true, search: '?jobs=4' })).toBe('threaded');
        expect(chooseWasmVariant({ crossOriginIsolated: true, search: '?jobs=0' })).toBe('single');
        expect(chooseWasmVariant({ crossOriginIsolated: false, search: '' })).toBe('single');
    });

    it('reads ?jobs=0 exactly', () => {
        expect(jobsDisabled('?jobs=0&perf=1')).toBe(true);
        expect(jobsDisabled('?jobs=00')).toBe(false);
        expect(jobsDisabled('')).toBe(false);
    });
});

describe('locateWasmFile', () => {
    const base = new URL('https://example.com/solar-system/');
    it('shares one .data between the variants and resolves binaries on the deployed base', () => {
        expect(locateWasmFile('SolarSystem-mt.data', '', base)).toBe(
            'https://example.com/solar-system/SolarSystem.data',
        );
        expect(locateWasmFile('SolarSystem.data', '', base)).toBe('https://example.com/solar-system/SolarSystem.data');
        expect(locateWasmFile('SolarSystem-mt.wasm', '', base)).toBe(
            'https://example.com/solar-system/SolarSystem-mt.wasm',
        );
        expect(locateWasmFile('other.js', '/x/', base)).toBe('/x/other.js');
    });
});

describe('parseGpuRequest', () => {
    it('reads ?gpu=webgpu and ?gpu=webgl, and treats anything else as auto', () => {
        expect(parseGpuRequest('?gpu=webgpu')).toBe('webgpu');
        expect(parseGpuRequest('?gpu=WGPU')).toBe('webgpu');
        expect(parseGpuRequest('?gpu=webgl')).toBe('webgl');
        expect(parseGpuRequest('?perf=1&gpu=webgl2')).toBe('webgl');
        expect(parseGpuRequest('?gpu=vulkan')).toBe('auto');
        expect(parseGpuRequest('')).toBe('auto');
    });
});

describe('chooseGpuBackend', () => {
    const everything = { webgpuAvailable: true, webgpuPublished: true };

    it('stays on WebGL 2 unless asked, while the WebGPU backend is not at parity', () => {
        expect(chooseGpuBackend({ search: '', ...everything }).backend).toBe('webgl2');
        expect(chooseGpuBackend({ search: '?gpu=webgl', ...everything }).backend).toBe('webgl2');
        expect(chooseGpuBackend({ search: '?gpu=webgpu', ...everything })).toEqual({
            backend: 'webgpu',
            reason: '?gpu=webgpu',
        });
    });

    it('falls back to WebGL 2 when the browser or the deployment cannot do WebGPU', () => {
        expect(chooseGpuBackend({ search: '?gpu=webgpu', webgpuAvailable: false, webgpuPublished: true })).toEqual({
            backend: 'webgl2',
            reason: 'no navigator.gpu in this browser',
        });
        expect(chooseGpuBackend({ search: '?gpu=webgpu', webgpuAvailable: true, webgpuPublished: false }).backend).toBe(
            'webgl2',
        );
    });

    it('lets auto pick WebGPU once the backend is switched on, and ?gpu=webgl still overrides', () => {
        expect(chooseGpuBackend({ search: '', ...everything, autoSelect: true })).toEqual({
            backend: 'webgpu',
            reason: 'auto',
        });
        expect(chooseGpuBackend({ search: '?gpu=webgl', ...everything, autoSelect: true }).backend).toBe('webgl2');
        expect(
            chooseGpuBackend({ search: '', webgpuAvailable: false, webgpuPublished: true, autoSelect: true }).backend,
        ).toBe('webgl2');
    });
});

describe('wasmGlue', () => {
    it('names the four binaries, bundling only the single-threaded WebGL 2 one', () => {
        expect(wasmGlue('webgl2', 'single')).toBeNull();
        expect(wasmGlue('webgl2', 'threaded')).toBe('SolarSystem-mt.js');
        expect(wasmGlue('webgpu', 'single')).toBe('SolarSystem-wgpu.js');
        expect(wasmGlue('webgpu', 'threaded')).toBe('SolarSystem-wgpu-mt.js');
    });

    it("shares each backend's .data between its two threading variants", () => {
        const base = new URL('https://example.com/solar-system/');
        expect(locateWasmFile('SolarSystem-wgpu-mt.data', '', base)).toBe(
            'https://example.com/solar-system/SolarSystem-wgpu.data',
        );
        expect(locateWasmFile('SolarSystem-wgpu.wasm', '', base)).toBe(
            'https://example.com/solar-system/SolarSystem-wgpu.wasm',
        );
    });
});
