#include "FrameTimer.h"
#include <thread>

FrameTimer::FrameTimer(uint16_t maxFps)
    : _frameTime(std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / maxFps)))
{
}

void FrameTimer::RunFrameTimer() {
    TryUpdateFps();
    _frameStartPoint = Clock::now();
}

void FrameTimer::EndFrame() {
    _tempFps++;
}

void FrameTimer::WaitForFrameTimer() const {
    std::this_thread::sleep_until(_frameStartPoint + _frameTime);
}

uint16_t FrameTimer::GetCurrentFps() const {
    return _currentFps;
}

void FrameTimer::TryUpdateFps() {
    if (std::chrono::duration<double>(Clock::now() - _tempStartPoint).count() >= 1.0) {
        _currentFps = _tempFps;
        _tempFps = 0;
        _tempStartPoint = Clock::now();
    }
}
