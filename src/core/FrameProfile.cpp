#include "FrameProfile.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
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
    std::string out = "{\"frameMs\":";
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
    out += '}';
    return out;
}

} // namespace FrameProfile
