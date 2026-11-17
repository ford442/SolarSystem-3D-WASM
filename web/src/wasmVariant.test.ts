import { describe, expect, it } from 'vitest';
import { chooseWasmVariant, jobsDisabled, locateWasmFile } from './wasmVariant';

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
