#include "GpuTimer.h"
#include "GlCapabilities.h"
#include "GlHandle.h"
#include "GlPlatform.h"
#include <cstddef>
#include <cstdint>
#include <vector>

#ifndef GL_TIME_ELAPSED
#define GL_TIME_ELAPSED 0x88BF // GL_TIME_ELAPSED_EXT: same value in the WebGL 2 extension
#endif
#ifndef GL_GPU_DISJOINT_EXT
#define GL_GPU_DISJOINT_EXT 0x8FBB
#endif

GpuTimer::GpuTimer(std::size_t stageCount) : _stageCount(stageCount), _resultMs(stageCount, 0.0) {
    for (Slot& slot : _slots) {
        slot.pool.resize(stageCount);
        slot.used.assign(stageCount, 0);
    }
}

bool GpuTimer::Supported() {
    return GetGlCapabilities().gpuTimerQuery;
}

void GpuTimer::SetEnabled(bool enabled) {
    _enabled = enabled && Supported();
    if (!_enabled) {
        _hasResult = false;
    }
}

bool GpuTimer::Harvest(Slot& slot) {
    // Every query in the set must be done; checking the last one alone is not enough on a
    // driver that retires them out of order.
    for (std::size_t stage = 0; stage < _stageCount; ++stage) {
        for (std::size_t i = 0; i < slot.used[stage]; ++i) {
            GLuint available = 0;
            glGetQueryObjectuiv(slot.pool[stage][i].get(), GL_QUERY_RESULT_AVAILABLE, &available);
            if (!available) {
                return false;
            }
        }
    }
    for (std::size_t stage = 0; stage < _stageCount; ++stage) {
        std::uint64_t ns = 0;
        for (std::size_t i = 0; i < slot.used[stage]; ++i) {
#ifdef __EMSCRIPTEN__
            // WebGL returns the result as a JS number; 32 bits of nanoseconds is 4.2 s per bracket.
            GLuint value = 0;
            glGetQueryObjectuiv(slot.pool[stage][i].get(), GL_QUERY_RESULT, &value);
            ns += value;
#else
            GLuint64 value = 0;
            glGetQueryObjectui64v(slot.pool[stage][i].get(), GL_QUERY_RESULT, &value);
            ns += value;
#endif
        }
        _resultMs[stage] = static_cast<double>(ns) / 1.0e6;
    }
    return true;
}

void GpuTimer::BeginFrame() {
    _recording = false;
    _open = -1;
    if (!_enabled) {
        return;
    }
    _current = (_current + 1) % _slots.size();
    Slot& slot = _slots[_current];

#ifdef __EMSCRIPTEN__
    // Reading GPU_DISJOINT clears it. A disjoint event invalidates every result in flight.
    GLint disjoint = 0;
    glGetIntegerv(GL_GPU_DISJOINT_EXT, &disjoint);
    if (disjoint) {
        for (Slot& other : _slots) {
            if (other.recorded && &other != &slot) {
                other.recorded = false;
            }
        }
        slot.recorded = false;
    }
#endif

    if (slot.recorded) {
        if (!Harvest(slot)) {
            return; // still in flight: this frame goes untimed rather than stalling on it
        }
        _hasResult = true;
        slot.recorded = false;
    }
    slot.used.assign(_stageCount, 0);
    _recording = true;
}

void GpuTimer::Begin(std::size_t stage) {
    if (!_recording || _open >= 0 || stage >= _stageCount) {
        return;
    }
    Slot& slot = _slots[_current];
    std::vector<GlQuery>& queries = slot.pool[stage];
    if (slot.used[stage] == queries.size()) {
        queries.push_back(GlQuery::Create());
    }
    glBeginQuery(GL_TIME_ELAPSED, queries[slot.used[stage]++].get());
    _open = static_cast<std::ptrdiff_t>(stage);
}

void GpuTimer::End(std::size_t stage) {
    if (_open != static_cast<std::ptrdiff_t>(stage)) {
        return;
    }
    glEndQuery(GL_TIME_ELAPSED);
    _open = -1;
}

void GpuTimer::EndFrame() {
    if (!_recording) {
        return;
    }
    if (_open >= 0) {
        End(static_cast<std::size_t>(_open));
    }
    Slot& slot = _slots[_current];
    for (const std::size_t used : slot.used) {
        if (used) {
            slot.recorded = true;
            break;
        }
    }
    _recording = false;
}
