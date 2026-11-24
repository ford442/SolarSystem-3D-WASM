#ifndef SOLARSYSTEM_GFX_RHI_WGPU_WGPUDEVICE_H
#define SOLARSYSTEM_GFX_RHI_WGPU_WGPUDEVICE_H
#include <webgpu/webgpu_cpp.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

/**
 * The WebGPU device the backend records against (docs/ARCHITECTURE.md §7.9). Only built with
 * CMake SOLARSYSTEM_WEBGPU=ON (emdawnwebgpu on the web, Dawn natively).
 *
 * On the web the page owns device creation, as it owns the WebGL context today
 * (web/src/webgpuContext.ts requests the adapter and device with the features it found and
 * hands it over as Module['preinitializedWebGPUDevice']); C++ only imports it. Natively the
 * device is requested here, synchronously: the instance is created with TimedWaitAny, so a
 * test or a golden capture can block on a future (a readback, an adapter) where a page never
 * could.
 */
namespace rhi::webgpu {

class Device {
public:
    struct Options {
        /** Ask for timestamp-query when the adapter has it (the perf HUD's per-pass GPU times). */
        bool timestamps = true;
        /** Native: prefer a software adapter (SwiftShader/lavapipe), as CI has no GPU. */
        bool forceFallbackAdapter = false;
    };

    /** The device, or null with `error` set when WebGPU is not available. */
    static std::unique_ptr<Device> Create(const Options& options, std::string& error);
    ~Device();
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;

    const wgpu::Instance& Instance() const { return _instance; }
    const wgpu::Device& Get() const { return _device; }
    const wgpu::Queue& Queue() const { return _queue; }
    bool HasTimestampQuery() const { return _timestamps; }
    /** "SwiftShader Device (Subzero) / Vulkan" etc., for logs and the perf HUD. */
    const std::string& AdapterName() const { return _adapterName; }
    /** minUniformBufferOffsetAlignment: dynamic uniform offsets are multiples of it. */
    std::uint32_t UniformOffsetAlignment() const { return _uniformAlignment; }

    /** Native: block until `future` completes (true) or the timeout passes. Web: always false. */
    bool Wait(const wgpu::Future& future, std::uint64_t timeoutNs = UINT64_MAX) const;
    /** Deliver callbacks that are ready (Instance::ProcessEvents); the browser does this itself. */
    void Poll() const;
    /** Validation and out-of-memory errors nobody caught, since the last call. */
    std::vector<std::string> TakeErrors();
    /** Whether the device has been lost (it then records nothing). */
    bool Lost() const { return _lost; }

private:
    Device() = default;
    void InstallCallbacks(wgpu::DeviceDescriptor& descriptor);

    wgpu::Instance _instance;
    wgpu::Adapter _adapter;
    wgpu::Device _device;
    wgpu::Queue _queue;
    bool _timestamps = false;
    bool _lost = false;
    std::string _adapterName;
    std::uint32_t _uniformAlignment = 256;
    std::mutex _errorsMutex; // Dawn may report errors from its own threads natively
    std::vector<std::string> _errors;
};

} // namespace rhi::webgpu

#endif // SOLARSYSTEM_GFX_RHI_WGPU_WGPUDEVICE_H
