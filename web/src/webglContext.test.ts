import { describe, expect, it } from 'vitest';
import { computeWebGlContextOptions, computeWebGlCostTier, type WebGlCostTier } from './webglContext.js';

const TIERS: WebGlCostTier[] = ['lowMobile', 'medium', 'full'];

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

describe('computeWebGlCostTier', () => {
    it('forces the cheapest tier on mobile and maps desktop presets 0/1/2', () => {
        expect(computeWebGlCostTier(2, true)).toBe('lowMobile');
        expect(computeWebGlCostTier(0, false)).toBe('lowMobile');
        expect(computeWebGlCostTier(1, false)).toBe('medium');
        expect(computeWebGlCostTier(2, false)).toBe('full');
    });
});
