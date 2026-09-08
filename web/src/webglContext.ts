import type { QualityPreset } from './SolarSystem.js';
import type { SolarSystemInitConfig } from './initialSettings';

/**
 * Single owner of GPU-cost tiering shared by WebGL context creation and backing-store
 * (device-pixel-ratio) sizing. Mirrors GetQualitySettings()'s mobile-forces-cheapest-tier
 * rule in QualitySettings.cpp (requestedMsaaSamples is 0 for every mobile preset, 4 only
 * for desktop "full") so TS and C++ agree without duplicating the whole quality table.
 */
export type WebGlCostTier = 'lowMobile' | 'medium' | 'full';

/** lib.dom.d.ts's WebGLContextAttributes predates the WebXR spec's xrCompatible flag. */
export interface WebGlContextOptions extends WebGLContextAttributes {
    xrCompatible?: boolean;
}

export function computeWebGlCostTier(qualityPreset: QualityPreset, isMobileWeb: boolean): WebGlCostTier {
    if (isMobileWeb || qualityPreset === 0) return 'lowMobile';
    return qualityPreset === 1 ? 'medium' : 'full';
}

/**
 * Context creation attributes are fixed for the lifetime of the page — WebGL contexts
 * cannot be recreated with new attributes, so MSAA/alpha/power preference are decided
 * once here, before Module() runs, from the same quality/mobile config C++ reads via
 * window.__solarSystemInit. See docs/plans/PORTING_GUIDE.md for why Module.contextAttributes
 * (passed to Emscripten's Module()) is NOT the mechanism for this: Emscripten's GLFW port
 * builds its own context-attributes object purely from GLFW window hints and ignores
 * Module.contextAttributes entirely. The supported hook is Module.preinitializedWebGLContext:
 * create the context ourselves with the exact attributes we want, and GLFW reuses it as-is.
 */
export function computeWebGlContextOptions(tier: WebGlCostTier): WebGlContextOptions {
    return {
        alpha: false,
        antialias: tier === 'full',
        depth: true,
        stencil: false,
        premultipliedAlpha: true,
        preserveDrawingBuffer: false,
        powerPreference: tier === 'lowMobile' ? 'low-power' : 'high-performance',
        // Enables WebXR's XRWebGLLayer to use this context directly; without this, WebXR
        // falls back to the slower async gl.makeXRCompatible() before entering a session.
        xrCompatible: true,
    };
}

/** Backing-store scale (canvas pixel buffer size vs. its CSS layout size), capped per tier. */
export function computeBackingStoreScale(tier: WebGlCostTier, devicePixelRatio: number): number {
    const cap = tier === 'lowMobile' ? 1 : tier === 'medium' ? 1.5 : 2;
    return Math.min(devicePixelRatio, cap);
}

export function computeWebGlSizing(config: SolarSystemInitConfig, devicePixelRatio = window.devicePixelRatio || 1) {
    const tier = computeWebGlCostTier(config.qualityPreset, config.isMobileWeb);
    return {
        tier,
        contextOptions: computeWebGlContextOptions(tier),
        backingStoreScale: computeBackingStoreScale(tier, devicePixelRatio),
    };
}
