#include "WgpuDevice.h"
#include "WgpuStrings.h"
#include "core/Log.h"

#include <webgpu/webgpu_cpp.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <webgpu/webgpu.h>
#endif

namespace rhi::webgpu {

namespace {

std::string DescribeAdapter(const wgpu::Adapter& adapter) {
    wgpu::AdapterInfo info{};
    if (adapter.GetInfo(&info) != wgpu::Status::Success) {
        return "unknown adapter";
    }
    std::string name = ToString(info.device);
    if (name.empty()) {
        name = ToString(info.description);
    }
    const char* backend = "";
    switch (info.backendType) {
        case wgpu::BackendType::Vulkan: backend = " / Vulkan"; break;
        case wgpu::BackendType::Metal: backend = " / Metal"; break;
        case wgpu::BackendType::D3D12: backend = " / D3D12"; break;
        case wgpu::BackendType::D3D11: backend = " / D3D11"; break;
        case wgpu::BackendType::WebGPU: backend = " / browser"; break;
        case wgpu::BackendType::Null: backend = " / null"; break;
        default: break;
    }
    return (name.empty() ? std::string("adapter") : name) + backend;
}

} // namespace

Device::~Device() = default;

void Device::InstallCallbacks(wgpu::DeviceDescriptor& descriptor) {
    descriptor.SetUncapturedErrorCallback(
        [](const wgpu::Device&, wgpu::ErrorType type, wgpu::StringView message, Device* self) {
            const std::string text = ToString(message);
            LOG_ERROR("WebGPU", "uncaptured %s error: %s",
                      type == wgpu::ErrorType::Validation    ? "validation"
                      : type == wgpu::ErrorType::OutOfMemory ? "out-of-memory"
                                                             : "internal",
                      text.c_str());
            const std::lock_guard<std::mutex> lock(self->_errorsMutex);
            self->_errors.push_back(text);
        },
        this);
    descriptor.SetDeviceLostCallback(
        wgpu::CallbackMode::AllowSpontaneous,
        [](const wgpu::Device&, wgpu::DeviceLostReason reason, wgpu::StringView message, Device* self) {
            if (reason != wgpu::DeviceLostReason::Destroyed) {
                LOG_ERROR("WebGPU", "device lost: %s", ToString(message).c_str());
            }
            self->_lost = true;
        },
        this);
}

std::unique_ptr<Device> Device::Create(const Options& options, std::string& error) {
    std::unique_ptr<Device> device(new Device());
#ifdef __EMSCRIPTEN__
    // web/src/webgpuContext.ts created the adapter and device (with timestamp-query when it
    // could) and published it as Module['preinitializedWebGPUDevice'] before main().
    (void)options;
    device->_instance = wgpu::CreateInstance(nullptr);
    WGPUDevice imported = emscripten_webgpu_get_device();
    if (!imported) {
        error = "no preinitialized WebGPU device (Module['preinitializedWebGPUDevice'])";
        return nullptr;
    }
    device->_device = wgpu::Device::Acquire(imported);
    device->_adapterName = "browser WebGPU";
#else
    static constexpr wgpu::InstanceFeatureName kWaitAny = wgpu::InstanceFeatureName::TimedWaitAny;
    wgpu::InstanceDescriptor instanceDescriptor{};
    instanceDescriptor.requiredFeatureCount = 1;
    instanceDescriptor.requiredFeatures = &kWaitAny;
    device->_instance = wgpu::CreateInstance(&instanceDescriptor);
    if (!device->_instance) {
        error = "wgpuCreateInstance failed";
        return nullptr;
    }

    wgpu::RequestAdapterOptions adapterOptions{};
    adapterOptions.powerPreference = wgpu::PowerPreference::HighPerformance;
    adapterOptions.forceFallbackAdapter = options.forceFallbackAdapter;
    std::string adapterError;
    const wgpu::Future adapterFuture = device->_instance.RequestAdapter(
        &adapterOptions, wgpu::CallbackMode::WaitAnyOnly,
        [&](wgpu::RequestAdapterStatus status, wgpu::Adapter adapter, wgpu::StringView message) {
            if (status == wgpu::RequestAdapterStatus::Success) {
                device->_adapter = std::move(adapter);
            } else {
                adapterError = ToString(message);
            }
        });
    if (device->_instance.WaitAny(adapterFuture, UINT64_MAX) != wgpu::WaitStatus::Success || !device->_adapter) {
        error = "no WebGPU adapter" + (adapterError.empty() ? std::string() : ": " + adapterError);
        return nullptr;
    }
    device->_adapterName = DescribeAdapter(device->_adapter);

    std::vector<wgpu::FeatureName> features;
    if (options.timestamps && device->_adapter.HasFeature(wgpu::FeatureName::TimestampQuery)) {
        features.push_back(wgpu::FeatureName::TimestampQuery);
    }
    for (const wgpu::FeatureName optional :
         {wgpu::FeatureName::TextureCompressionBC, wgpu::FeatureName::TextureCompressionETC2,
          wgpu::FeatureName::TextureCompressionASTC, wgpu::FeatureName::Float32Filterable}) {
        if (device->_adapter.HasFeature(optional)) {
            features.push_back(optional);
        }
    }
    wgpu::DeviceDescriptor deviceDescriptor{};
    deviceDescriptor.label = "SolarSystem";
    deviceDescriptor.requiredFeatureCount = features.size();
    deviceDescriptor.requiredFeatures = features.data();
    device->InstallCallbacks(deviceDescriptor);
    std::string deviceError;
    const wgpu::Future deviceFuture = device->_adapter.RequestDevice(
        &deviceDescriptor, wgpu::CallbackMode::WaitAnyOnly,
        [&](wgpu::RequestDeviceStatus status, wgpu::Device created, wgpu::StringView message) {
            if (status == wgpu::RequestDeviceStatus::Success) {
                device->_device = std::move(created);
            } else {
                deviceError = ToString(message);
            }
        });
    if (device->_instance.WaitAny(deviceFuture, UINT64_MAX) != wgpu::WaitStatus::Success || !device->_device) {
        error = "WebGPU device request failed" + (deviceError.empty() ? std::string() : ": " + deviceError);
        return nullptr;
    }
#endif
    device->_queue = device->_device.GetQueue();
    device->_timestamps = device->_device.HasFeature(wgpu::FeatureName::TimestampQuery);
    wgpu::Limits limits{};
    if (device->_device.GetLimits(&limits) == wgpu::Status::Success) {
        device->_uniformAlignment = limits.minUniformBufferOffsetAlignment;
    }
    LOG_INFO("WebGPU", "device on %s%s", device->_adapterName.c_str(),
             device->_timestamps ? " (timestamp queries)" : "");
    return device;
}

bool Device::Wait(const wgpu::Future& future, std::uint64_t timeoutNs) const {
#ifdef __EMSCRIPTEN__
    (void)future;
    (void)timeoutNs;
    return false; // the page's event loop delivers callbacks; nothing may block it
#else
    return _instance.WaitAny(future, timeoutNs) == wgpu::WaitStatus::Success;
#endif
}

void Device::Poll() const {
#ifndef __EMSCRIPTEN__
    _instance.ProcessEvents();
#endif
}

std::vector<std::string> Device::TakeErrors() {
    const std::lock_guard<std::mutex> lock(_errorsMutex);
    std::vector<std::string> errors;
    errors.swap(_errors);
    return errors;
}

} // namespace rhi::webgpu
