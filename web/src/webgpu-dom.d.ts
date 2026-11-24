/**
 * Minimal WebGPU DOM typings: TypeScript's lib.dom does not ship them yet, and the page only
 * touches the adapter/device request (webgpuContext.ts); C++ drives everything else through
 * emdawnwebgpu. Full @webgpu/types is optional; like webxr-dom.d.ts, these cover what we use.
 */

type GPUFeatureName = string;
type GPUPowerPreference = 'low-power' | 'high-performance';

interface GPURequestAdapterOptions {
    powerPreference?: GPUPowerPreference;
    forceFallbackAdapter?: boolean;
}

interface GPUAdapterInfo {
    readonly vendor: string;
    readonly architecture: string;
    readonly device: string;
    readonly description: string;
}

interface GPUDeviceDescriptor {
    label?: string;
    requiredFeatures?: Iterable<GPUFeatureName>;
}

interface GPUDeviceLostInfo {
    readonly reason: 'unknown' | 'destroyed';
    readonly message: string;
}

interface GPUError {
    readonly message: string;
}

interface GPUUncapturedErrorEvent extends Event {
    readonly error: GPUError;
}

interface GPUDevice extends EventTarget {
    readonly features: ReadonlySet<string>;
    readonly lost: Promise<GPUDeviceLostInfo>;
    destroy(): void;
    addEventListener(type: 'uncapturederror', listener: (event: GPUUncapturedErrorEvent) => void): void;
}

interface GPUAdapter {
    readonly features: ReadonlySet<string>;
    /** Chrome 114+, Firefox 133+, Safari 26; older browsers only have requestAdapterInfo(). */
    readonly info?: GPUAdapterInfo;
    readonly isFallbackAdapter?: boolean;
    requestDevice(descriptor?: GPUDeviceDescriptor): Promise<GPUDevice>;
}

interface GPU {
    requestAdapter(options?: GPURequestAdapterOptions): Promise<GPUAdapter | null>;
    getPreferredCanvasFormat(): string;
}

interface Navigator {
    readonly gpu?: GPU;
}
