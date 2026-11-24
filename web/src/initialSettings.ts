import { parseDeepLinkFromUrl } from './deepLink';
import { isMobileLikeDevice } from './touchControls';
import type { QualityPreset } from './SolarSystem.js';
import { computeBackingStoreScale, computeGpuCostTier } from './gpuContext';

/** Values consumed by C++ before GLFW / WebGL context creation (see QualitySettings.cpp). */
export interface SolarSystemInitConfig {
    qualityPreset: QualityPreset;
    isMobileWeb: boolean;
    /** Canvas backing-store pixels per CSS pixel, capped per quality tier. See gpuContext.ts. */
    backingStoreScale: number;
}

/**
 * Resolve quality and mobile flags once in TypeScript before Module() starts.
 * URL deep-link quality wins over device defaults; localStorage is applied later via the bridge.
 */
export function resolveInitConfig(): SolarSystemInitConfig {
    const deepLink = parseDeepLinkFromUrl();
    const isMobileWeb = isMobileLikeDevice();
    const defaultQuality: QualityPreset = isMobileWeb ? 0 : 2;
    const qualityPreset = deepLink.quality ?? defaultQuality;
    const tier = computeGpuCostTier(qualityPreset, isMobileWeb);
    const backingStoreScale = computeBackingStoreScale(tier, window.devicePixelRatio || 1);
    return { qualityPreset, isMobileWeb, backingStoreScale };
}

/** Publish init config for C++ EM_ASM readers; call synchronously before Module(). */
export function publishInitConfig(config: SolarSystemInitConfig): void {
    window.__solarSystemInit = config;
}
