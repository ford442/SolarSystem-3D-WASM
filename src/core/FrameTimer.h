#ifndef SOLARSYSTEM_FPS_LIMITER_H
#define SOLARSYSTEM_FPS_LIMITER_H
#include <chrono>
#include <cstdint>

using namespace std::chrono;

class FPS_Handler {
public:
    explicit FPS_Handler(uint16_t maxFps);
    void RunFrameTimer();
    // Counts the frame toward GetCurrentFps(). Both targets call it once per presented frame.
    void EndFrame();
    // Native frame limiter: sleeps (does not spin) until the frame's time budget is used up.
    // Web never calls it — requestAnimationFrame paces the loop there.
    void WaitForFrameTimer() const;
    uint16_t GetCurrentFps() const;

private:
    steady_clock::duration _frameTime;
    uint16_t _currentFps = 0; // Updated once per second
    uint16_t _tempFps = 0;
    steady_clock::time_point _frameStartPoint, _tempStartPoint; // _tempStartPoint is needed to check if a second has passed

    void TryUpdateFps();
};

#endif //SOLARSYSTEM_FPS_LIMITER_H
