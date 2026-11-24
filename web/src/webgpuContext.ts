/**
 * The page's half of the WebGPU backend (docs/plans/WEBGPU_BACKEND_PLAN.md): request the
 * adapter and the device, the WebGPU mirror of creating the WebGL 2 context ourselves
 * (gpuContext.ts). The device goes to C++ as Module['preinitializedWebGPUDevice'], which
 * emdawnwebgpu's emscripten_webgpu_get_device() imports (gfx/rhi/wgpu/WgpuDevice.cpp); C++ then
 * configures the canvas and every target itself.
 *
 * Only the SolarSystem-wgpu binary takes a device, and wasmVariant.ts only picks that binary for
 * `?gpu=webgpu` on a deployment that publishes it. A failure anywhere here returns null and the
 * page carries on with the WebGL 2 build.
 */

/**
 * Optional features the backend uses when the adapter has them, as WgpuDevice.cpp asks natively:
 * per-pass GPU times for the perf HUD, the compressed texture packs (assets/TextureFormatSupport),
 * and filterable float32 (the atmosphere LUTs).
 */
export const OPTIONAL_WEBGPU_FEATURES = [
    'timestamp-query',
    'texture-compression-bc',
    'texture-compression-etc2',
    'texture-compression-astc',
    'float32-filterable',
] as const;

/** The optional features `available` has, in OPTIONAL_WEBGPU_FEATURES order. */
export function chooseWebGpuFeatures(available: ReadonlySet<string>): string[] {
    return OPTIONAL_WEBGPU_FEATURES.filter((feature) => available.has(feature));
}

/** "Google · swiftshader (fallback)" from whatever the adapter tells about itself. */
export function describeAdapter(adapter: Pick<GPUAdapter, 'info' | 'isFallbackAdapter'>): string {
    const info = adapter.info;
    const parts = info
        ? [info.vendor, info.architecture, info.device || info.description].filter((part) => part.length > 0)
        : [];
    const name = parts.length > 0 ? parts.join(' · ') : 'WebGPU adapter';
    return adapter.isFallbackAdapter ? `${name} (fallback)` : name;
}

export interface WebGpuDeviceResult {
    device: GPUDevice;
    adapter: string;
    features: string[];
}

export interface WebGpuRequestDeps {
    gpu: GPU | undefined;
    adapterOptions: GPURequestAdapterOptions;
    log?: (message: string) => void;
    warn?: (message: string) => void;
}

/**
 * The device for the WebGPU binary, or null (with a warning) when the browser has no WebGPU, no
 * adapter, or refuses the device. Uncaptured errors and device loss are logged: C++ cannot install
 * those callbacks on a device it did not create.
 */
export async function requestWebGpuDevice({
    gpu,
    adapterOptions,
    log = (message) => console.log(message),
    warn = (message) => console.warn(message),
}: WebGpuRequestDeps): Promise<WebGpuDeviceResult | null> {
    if (!gpu) {
        warn('[WebGPU] navigator.gpu is not available');
        return null;
    }
    try {
        const adapter = await gpu.requestAdapter(adapterOptions);
        if (!adapter) {
            warn('[WebGPU] no adapter');
            return null;
        }
        const features = chooseWebGpuFeatures(adapter.features);
        const device = await adapter.requestDevice({ label: 'SolarSystem', requiredFeatures: features });
        device.addEventListener('uncapturederror', (event) => {
            console.error('[WebGPU] uncaptured error:', event.error.message);
        });
        void device.lost.then((info) => {
            if (info.reason !== 'destroyed') {
                warn(`[WebGPU] device lost: ${info.message}`);
            }
        });
        const name = describeAdapter(adapter);
        log(`[WebGPU] device on ${name}${features.length > 0 ? ` (${features.join(', ')})` : ''}`);
        return { device, adapter: name, features };
    } catch (error: unknown) {
        warn(`[WebGPU] device request failed: ${error instanceof Error ? error.message : String(error)}`);
        return null;
    }
}
