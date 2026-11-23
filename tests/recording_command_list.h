#ifndef SOLARSYSTEM_TESTS_RECORDING_COMMAND_LIST_H
#define SOLARSYSTEM_TESTS_RECORDING_COMMAND_LIST_H
#include "gfx/rhi/CommandList.h"
#include "gfx/rhi/RhiTypes.h"

#include <cstdint>
#include <string>
#include <vector>

/** An rhi::CommandList that writes each call down as a token, for the frame-graph tests. */
class RecordingCommandList : public rhi::CommandList {
public:
    std::vector<std::string> log;
    std::vector<rhi::PassInfo> passes;

    void BeginPass(const rhi::PassInfo& pass) override {
        passes.push_back(pass);
        log.push_back(std::string("begin ") + pass.name);
    }
    void EndPass() override { log.emplace_back("end"); }
    void SetRenderTarget(const rhi::RenderTarget& target) override {
        log.push_back("target " + std::to_string(target.framebuffer));
    }
    void BindFramebuffer(rhi::FramebufferId framebuffer) override {
        log.push_back("framebuffer " + std::to_string(framebuffer));
    }
    void SetViewport(std::int32_t, std::int32_t, std::int32_t, std::int32_t) override { log.emplace_back("viewport"); }
    void Clear(rhi::ClearFlags) override { log.emplace_back("clear"); }
    void SetPipelineState(const rhi::PipelineState&) override { log.emplace_back("state"); }
    void SetBindGroup(const rhi::BindGroup&) override { log.emplace_back("bind"); }
    void DrawFullscreenTriangle(rhi::ObjectId) override { log.emplace_back("draw"); }
};

#endif // SOLARSYSTEM_TESTS_RECORDING_COMMAND_LIST_H
