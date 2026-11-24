#include "FrameProfile.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <ratio>
#include <string>

namespace FrameProfile {

const char* StageName(Stage stage) {
    switch (stage) {
        case Stage::Update: return "update";
        case Stage::Background: return "background";
        case Stage::Shadow: return "shadow";
        case Stage::Scene: return "scene";
        case Stage::Foreground: return "foreground";
        case Stage::Hud: return "hud";
        case Stage::Uploads: return "uploads";
        case Stage::SpaceWeather: return "spaceWeather";
        case Stage::Count: break;
    }
    return "?";
}

void Totals::Add(const Sample& sample) {
    ++frames;
    if (sample.frameMs > kSlowFrameMs) {
        ++slowFrames;
    }
    worstFrameMs = std::max(worstFrameMs, sample.frameMs);
}

namespace {

void AppendNumber(std::string& out, double value) {
    if (!std::isfinite(value)) {
        out += '0';
        return;
    }
    std::array<char, 32> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "%.3f", value);
    out += buffer.data();
}

void AppendStages(std::string& out, const std::array<double, kStageCount>& ms) {
    out += '{';
    for (std::size_t i = 0; i < kStageCount; ++i) {
        if (i) {
            out += ',';
        }
        out += '"';
        out += StageName(static_cast<Stage>(i));
        out += "\":";
        AppendNumber(out, ms[i]);
    }
    out += '}';
}

} // namespace

std::string ToJson(const Sample& sample, const Totals& totals) {
    // Ids and names come from rhi::BackendId/BackendName: plain identifiers, nothing to escape.
    std::string out = "{\"backend\":\"";
    out += sample.backend;
    out += "\",\"backendName\":\"";
    out += sample.backendName;
    out += "\",\"webgpu\":";
    out += sample.webgpuJson.empty() ? "null" : sample.webgpuJson;
    out += ",\"frameMs\":";
    AppendNumber(out, sample.frameMs);
    out += ",\"worstFrameMs\":";
    AppendNumber(out, totals.worstFrameMs);
    out += ",\"frames\":" + std::to_string(totals.frames);
    out += ",\"slowFrames\":" + std::to_string(totals.slowFrames);
    out += ",\"cpu\":";
    AppendStages(out, sample.cpuMs);
    out += ",\"gpuSupported\":";
    out += sample.gpuSupported ? "true" : "false";
    out += ",\"gpu\":";
    if (sample.gpuValid) {
        AppendStages(out, sample.gpuMs);
    } else {
        out += "null";
    }
    out += R"(,"jobs":{"workers":)" + std::to_string(sample.jobWorkers) +
           ",\"queued\":" + std::to_string(sample.jobQueueDepth) +
           ",\"inFlight\":" + std::to_string(sample.jobsInFlight) +
           ",\"completed\":" + std::to_string(sample.jobsCompleted) + '}';
    out += R"(,"uploads":{"bytes":)" + std::to_string(sample.uploadBytes) +
           ",\"pending\":" + std::to_string(sample.uploadsPending) + '}';
    out += ",\"uniformCalls\":" + std::to_string(sample.uniformCalls);
    out += ",\"passes\":[";
    for (std::size_t i = 0; i < sample.passes.size(); ++i) {
        const PassSample& pass = sample.passes[i];
        if (i) {
            out += ',';
        }
        out += "{\"kind\":\"" + pass.kind + "\",\"stage\":\"" + StageName(pass.stage) +
               "\",\"count\":" + std::to_string(pass.count) + ",\"cpu\":";
        AppendNumber(out, pass.cpuMs);
        out += ",\"gpu\":";
        if (sample.gpuValid) {
            AppendNumber(out, pass.gpuMs);
        } else {
            out += "null";
        }
        out += '}';
    }
    out += "]}";
    return out;
}

int PassTimings::SlotFor(const std::string& kind, Stage stage) {
    for (std::size_t i = 0; i < _kinds.size(); ++i) {
        if (_kinds[i] == kind) {
            return static_cast<int>(i);
        }
    }
    if (_kinds.size() >= kMaxPassKinds) {
        return static_cast<int>(kMaxPassKinds - 1);
    }
    _kinds.push_back(kind);
    _stages.push_back(stage);
    _cpuMs.push_back(0.0);
    _runs.push_back(0);
    return static_cast<int>(_kinds.size() - 1);
}

void PassTimings::Reset() {
    std::fill(_cpuMs.begin(), _cpuMs.end(), 0.0);
    std::fill(_runs.begin(), _runs.end(), 0);
}

void PassTimings::Add(int slot, Clock::duration elapsed) {
    if (slot < 0 || static_cast<std::size_t>(slot) >= _kinds.size()) {
        return;
    }
    const auto index = static_cast<std::size_t>(slot);
    _cpuMs[index] += std::chrono::duration<double, std::milli>(elapsed).count();
    ++_runs[index];
}

} // namespace FrameProfile
