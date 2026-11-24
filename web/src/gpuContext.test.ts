import { describe, expect, it } from 'vitest';
import {
    computeGpuCostTier,
    computeWebGlContextOptions,
    computeWebGpuAdapterOptions,
    type GpuCostTier,
} from './gpuContext.js';

const TIERS: GpuCostTier[] = ['lowMobile', 'medium', 'full'];

describe('computeWebGlContextOptions', () => {
    it('never multisamples the default framebuffer: HDR tiers antialias in the tone-map pass', () => {
        for (const tier of TIERS) {
            expect(computeWebGlContextOptions(tier).antialias, tier).toBe(false);
        }
    });

    it('keeps the attributes the frame loop and capture rely on', () => {
        for (const tier of TIERS) {
            const options = computeWebGlContextOptions(tier);
            expect(options.alpha).toBe(false);
            expect(options.depth).toBe(true);
            expect(options.stencil).toBe(false);
            // Capture reads the canvas in the same turn it draws; a preserved buffer is a copy.
            expect(options.preserveDrawingBuffer).toBe(false);
            expect(options.xrCompatible).toBe(true);
            // Inert while alpha is false, stated so the object matches what the browser uses.
            expect(options.premultipliedAlpha).toBe(false);
        }
    });

    it('asks for the low-power GPU only on the low mobile tier', () => {
        expect(computeWebGlContextOptions('lowMobile').powerPreference).toBe('low-power');
        expect(computeWebGlContextOptions('medium').powerPreference).toBe('high-performance');
        expect(computeWebGlContextOptions('full').powerPreference).toBe('high-performance');
    });
});

describe('computeGpuCostTier', () => {
    it('forces the cheapest tier on mobile and maps desktop presets 0/1/2', () => {
        expect(computeGpuCostTier(2, true)).toBe('lowMobile');
        expect(computeGpuCostTier(0, false)).toBe('lowMobile');
        expect(computeGpuCostTier(1, false)).toBe('medium');
        expect(computeGpuCostTier(2, false)).toBe('full');
    });
});

describe('computeWebGpuAdapterOptions', () => {
    it('asks the WebGPU adapter for the same GPU the WebGL context would get', () => {
        for (const tier of TIERS) {
            expect(computeWebGpuAdapterOptions(tier).powerPreference, tier).toBe(
                computeWebGlContextOptions(tier).powerPreference,
            );
        }
    });
});
