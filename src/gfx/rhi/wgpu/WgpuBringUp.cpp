#include "WgpuBringUp.h"
#include "WgpuStrings.h"
#include "WgpuDevice.h"
#include "WgpuProgram.h"
#include "core/Log.h"
#include "gfx/ShaderSource.h"

#include <webgpu/webgpu_cpp.h>

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace rhi::webgpu {

BringUp::~BringUp() = default;

std::unique_ptr<BringUp> BringUp::Start(const std::string& wgslDirectory) {
    std::string error;
    std::unique_ptr<BringUp> bringUp(new BringUp());
    bringUp->_device = Device::Create({}, error);
    if (!bringUp->_device) {
        LOG_WARN("WebGPU", "no device: %s", error.c_str());
        return nullptr;
    }
    ShaderManifest manifest;
    if (!manifest.Load(wgslDirectory, error)) {
        LOG_WARN("WebGPU", "%s", error.c_str());
        return bringUp; // the device still shows in the perf HUD
    }
    BringUp* self = bringUp.get();
    for (const auto& [name, stage] : manifest.Stages()) {
        const std::optional<std::string> code = ShaderSource::ReadFileFromDisk(wgslDirectory + "/" + name);
        if (!code) {
            ++self->_failed;
            continue;
        }
        wgpu::ShaderSourceWGSL wgsl{};
        wgsl.code = code->c_str();
        wgpu::ShaderModuleDescriptor descriptor{};
        descriptor.nextInChain = &wgsl;
        descriptor.label = name.c_str();
        wgpu::ShaderModule module = self->_device->Get().CreateShaderModule(&descriptor);
        ++self->_total;
        // Results arrive as the browser (or Dawn's ProcessEvents) gets to them; the module
        // handle is kept until then, the BringUp lives as long as Application.
        module.GetCompilationInfo(
            wgpu::CallbackMode::AllowSpontaneous,
            [self, name](wgpu::CompilationInfoRequestStatus status, const wgpu::CompilationInfo* info) {
                bool failed = status != wgpu::CompilationInfoRequestStatus::Success;
                for (std::size_t m = 0; info && m < info->messageCount; ++m) {
                    if (info->messages[m].type == wgpu::CompilationMessageType::Error) {
                        failed = true;
                        LOG_ERROR("ShaderSelfTest", "WGSL %s: %s", name.c_str(),
                                  ToString(info->messages[m].message).c_str());
                    }
                }
                ++(failed ? self->_failed : self->_compiled);
                self->ReportIfDone();
            });
        self->_modules.push_back(std::move(module));
    }
    // Dawn may answer inside GetCompilationInfo, before the loop has counted every module.
    self->_submitted = true;
    self->ReportIfDone();
    self->_device->Poll();
    return bringUp;
}

void BringUp::ReportIfDone() {
    if (_reported || !_submitted || _compiled + _failed < _total) {
        return;
    }
    _reported = true;
    LOG_INFO("ShaderSelfTest", "%d/%d WGSL modules compiled on %s%s", _compiled, _total, _device->AdapterName().c_str(),
             _failed == 0 ? " OK" : " - FAILURES");
}

std::string BringUp::StatusJson() const {
    _device->Poll(); // natively, deliver compilation results that are ready
    std::string json = "{\"device\":\"";
    for (const char c : _device->AdapterName()) { // an adapter's own name: escape what JSON needs
        if (c == '"' || c == '\\') {
            json += '\\';
        }
        if (static_cast<unsigned char>(c) >= 0x20) {
            json += c;
        }
    }
    json += "\",\"timestamps\":";
    json += _device->HasTimestampQuery() ? "true" : "false";
    json += ",\"wgslTotal\":" + std::to_string(_total) + ",\"wgslCompiled\":" + std::to_string(_compiled) +
            ",\"wgslFailed\":" + std::to_string(_failed) + "}";
    return json;
}

} // namespace rhi::webgpu
