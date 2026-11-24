import { describe, expect, it, vi } from 'vitest';
import { chooseWebGpuFeatures, describeAdapter, OPTIONAL_WEBGPU_FEATURES, requestWebGpuDevice } from './webgpuContext';

function fakeDevice(): GPUDevice {
    const target = new EventTarget();
    return Object.assign(target, {
        features: new Set<string>(),
        lost: new Promise<GPUDeviceLostInfo>(() => undefined),
        destroy: () => undefined,
    }) as unknown as GPUDevice;
}

function fakeGpu(
    adapter: Partial<GPUAdapter> | null,
    requestDevice = vi.fn(() => Promise.resolve(fakeDevice())),
): { gpu: GPU; requestAdapter: ReturnType<typeof vi.fn> } {
    const requestAdapter = vi.fn(() =>
        Promise.resolve(adapter ? { features: new Set<string>(), requestDevice, ...adapter } : null),
    );
    return { gpu: { requestAdapter, getPreferredCanvasFormat: () => 'bgra8unorm' }, requestAdapter };
}

describe('chooseWebGpuFeatures', () => {
    it('asks only for the optional features the adapter has, in a fixed order', () => {
        expect(chooseWebGpuFeatures(new Set(['texture-compression-astc', 'timestamp-query', 'shader-f16']))).toEqual([
            'timestamp-query',
            'texture-compression-astc',
        ]);
        expect(chooseWebGpuFeatures(new Set())).toEqual([]);
        expect(OPTIONAL_WEBGPU_FEATURES).toContain('timestamp-query');
    });
});

describe('describeAdapter', () => {
    it('names the adapter from its info and marks a fallback one', () => {
        expect(
            describeAdapter({
                info: { vendor: 'google', architecture: 'swiftshader', device: '', description: 'SwiftShader' },
                isFallbackAdapter: true,
            }),
        ).toBe('google · swiftshader · SwiftShader (fallback)');
        expect(describeAdapter({})).toBe('WebGPU adapter');
    });
});

describe('requestWebGpuDevice', () => {
    const quiet = { log: () => undefined, warn: () => undefined };

    it('returns null without navigator.gpu or an adapter', async () => {
        expect(await requestWebGpuDevice({ gpu: undefined, adapterOptions: {}, ...quiet })).toBeNull();
        expect(await requestWebGpuDevice({ gpu: fakeGpu(null).gpu, adapterOptions: {}, ...quiet })).toBeNull();
    });

    it('requests the device with the features it found and the tier power preference', async () => {
        const requestDevice = vi.fn((_descriptor?: GPUDeviceDescriptor) => Promise.resolve(fakeDevice()));
        const { gpu, requestAdapter } = fakeGpu(
            { features: new Set(['timestamp-query', 'texture-compression-bc']) },
            requestDevice,
        );
        const result = await requestWebGpuDevice({ gpu, adapterOptions: { powerPreference: 'low-power' }, ...quiet });
        expect(result?.features).toEqual(['timestamp-query', 'texture-compression-bc']);
        expect(requestAdapter).toHaveBeenCalledWith({ powerPreference: 'low-power' });
        expect(requestDevice.mock.calls[0]?.[0]?.requiredFeatures).toEqual([
            'timestamp-query',
            'texture-compression-bc',
        ]);
    });

    it('turns a refused device into null so the page falls back to WebGL 2', async () => {
        const warn = vi.fn();
        const { gpu } = fakeGpu(
            {},
            vi.fn(() => Promise.reject(new Error('limits'))),
        );
        expect(await requestWebGpuDevice({ gpu, adapterOptions: {}, log: () => undefined, warn })).toBeNull();
        expect(warn).toHaveBeenCalledWith('[WebGPU] device request failed: limits');
    });
});
