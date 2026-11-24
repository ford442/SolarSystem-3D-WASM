#ifndef SOLARSYSTEM_GFX_RHI_WGPU_WGPUBRINGUP_H
#define SOLARSYSTEM_GFX_RHI_WGPU_WGPUBRINGUP_H
#include <webgpu/webgpu_cpp.h>

#include <memory>
#include <string>
#include <vector>

namespace rhi::webgpu {

class Device;

/**
 * The WebGPU backend inside the running app, phase 1 (docs/plans/WEBGPU_BACKEND_PLAN.md):
 * the device (the page's on the web — the SolarSystem-wgpu binary — or Dawn's natively with
 * SOLARSYSTEM_GPU=webgpu), and every WGSL module of resource/shaders-wgsl compiled on it in the
 * background. On the web that is the browser's own WGSL compiler (Tint in Chrome, Naga in
 * Firefox, WebKit's in Safari), which the native Dawn tests cannot vouch for.
 *
 * The frame itself still records through GlCommandList until the passes move onto the RHI's
 * draw calls (phase 2); StatusJson() is what the perf HUD shows meanwhile.
 */
class BringUp {
public:
    /** Null (logged) if there is no device or no manifest. */
    static std::unique_ptr<BringUp> Start(const std::string& wgslDirectory);
    ~BringUp();
    BringUp(const BringUp&) = delete;
    BringUp& operator=(const BringUp&) = delete;

    /**
     * {"device":…,"timestamps":…,"wgslTotal":n,"wgslCompiled":n,"wgslFailed":n}: the frame
     * stats' "webgpu" member (FrameProfile::Sample::webgpuJson, web/src/perfHud.ts).
     */
    std::string StatusJson() const;
    Device& GetDevice() { return *_device; }

private:
    BringUp() = default;
    void ReportIfDone();

    std::unique_ptr<Device> _device;
    std::vector<wgpu::ShaderModule>
        _modules; // kept for the app's life: a few hundred KB, and no release inside a callback
    int _total = 0;
    int _compiled = 0;
    int _failed = 0;
    bool _submitted = false; // every module created
    bool _reported = false;
};

} // namespace rhi::webgpu

#endif // SOLARSYSTEM_GFX_RHI_WGPU_WGPUBRINGUP_H
