#ifndef SOLARSYSTEM_CORE_FRAMETIMER_H
#define SOLARSYSTEM_CORE_FRAMETIMER_H
#include <chrono>
#include <cstdint>

/** Frame pacing (native) and the once-a-second FPS count both targets show. */
class FrameTimer {
public:
    explicit FrameTimer(uint16_t maxFps);
    void RunFrameTimer();
    // Counts the frame toward GetCurrentFps(). Both targets call it once per presented frame.
    void EndFrame();
    // Native frame limiter: sleeps (does not spin) until the frame's time budget is used up.
    // Web never calls it — requestAnimationFrame paces the loop there.
    void WaitForFrameTimer() const;
    uint16_t GetCurrentFps() const;

private:
    using Clock = std::chrono::steady_clock;

    Clock::duration _frameTime;
    uint16_t _currentFps = 0; // Updated once per second
    uint16_t _tempFps = 0;
    Clock::time_point _frameStartPoint, _tempStartPoint; // _tempStartPoint is needed to check if a second has passed

    void TryUpdateFps();
};

#endif // SOLARSYSTEM_CORE_FRAMETIMER_H
