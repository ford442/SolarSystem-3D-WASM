// Fixed-step capture for web timelines and video (docs/ARCHITECTURE.md §11.2), and the native
// golden capture's readback (scripts/golden-native.sh). web/src/capture.ts drives Begin/Step/End
// through the BeginCapture/StepCaptureFrame/EndCapture exports.
#include "Cinematics.h"
#include "GoldenCapture.h"
#include "core/Log.h"
#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <sstream>
#include <string>
#include <utility>
#ifdef __EMSCRIPTEN__
#include <emscripten/html5.h>
#else
#include "gfx/GlPlatform.h"
#include <fstream>
#include <ios>
#include <vector>
#endif

void Cinematics::Begin(int width, int height) {
    if (_host.XrPresenting()) {
        LOG_WARN("Capture", "Not started: a WebXR session is presenting");
        return;
    }
    _active = true;
    _stepSeconds = 0.0;
    _host.ResetAnimationClock();
#ifdef __EMSCRIPTEN__
    if (width > 0 && height > 0) {
        _resized = true;
        _width = width;
        _height = height;
        emscripten_set_canvas_element_size("#canvas", width, height);
        _host.ResizeRenderTarget(width, height);
    }
#else
    (void)width;
    (void)height;
#endif
    const glm::ivec2 size = _host.RenderTargetSize();
    LOG_INFO("Capture", "Started at %dx%d", size.x, size.y);
}

void Cinematics::Step(double deltaSeconds) {
    if (!_active) {
        return;
    }
    _stepSeconds = std::max(0.0, deltaSeconds);
#ifdef __EMSCRIPTEN__
    if (_resized) {
        // Nothing should resize the canvas mid-capture, but a page layout change or a browser
        // callback that slipped in would cost the video a wrong frame: put the size back.
        int canvasWidth = 0, canvasHeight = 0;
        emscripten_get_canvas_element_size("#canvas", &canvasWidth, &canvasHeight);
        if (canvasWidth != _width || canvasHeight != _height) {
            emscripten_set_canvas_element_size("#canvas", _width, _height);
        }
        const glm::ivec2 size = _host.RenderTargetSize();
        if (canvasWidth != _width || canvasHeight != _height || size.x != _width || size.y != _height) {
            LOG_WARN("Capture", "Render size drifted to %dx%d; restoring %dx%d", canvasWidth, canvasHeight, _width,
                     _height);
            _host.ResizeRenderTarget(_width, _height);
        }
    }
#endif
    _host.RunCaptureFrame();
}

void Cinematics::End() {
    if (!_active) {
        return;
    }
    _active = false;
    // The next live frame measures from now, not from before the capture began.
    _host.RestartFrameClock();
    if (_resized) {
        _resized = false;
        _host.FitRenderTargetToPage();
    }
    LOG_INFO("Capture", "Ended");
}

#ifndef __EMSCRIPTEN__
void Cinematics::BeforeSwap(glm::ivec2 size) {
    if (_readbackPath.empty()) {
        return;
    }
    const auto width = static_cast<std::size_t>(size.x);
    const auto height = static_cast<std::size_t>(size.y);
    std::vector<unsigned char> pixels(width * height * 3);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, size.x, size.y, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    std::ofstream file(_readbackPath, std::ios::binary);
    file << "P6\n" << width << ' ' << height << "\n255\n";
    const std::size_t row = width * 3;
    for (std::size_t y = height; y-- > 0;) {
        file.write(reinterpret_cast<const char*>(pixels.data() + y * row), static_cast<std::streamsize>(row));
    }
    LOG_INFO("Golden", "Wrote %s (%dx%d)", _readbackPath.c_str(), size.x, size.y);
    _readbackPath.clear();
}

GoldenSpec ParseGoldenSpec(const std::string& spec) {
    GoldenSpec golden;
    std::stringstream stream(spec);
    std::string token;
    while (std::getline(stream, token, ',')) {
        const std::size_t eq = token.find('=');
        const std::string key = token.substr(0, eq);
        const std::string value = eq == std::string::npos ? std::string() : token.substr(eq + 1);
        if (key == "frames") {
            golden.frames = std::max(1, std::atoi(value.c_str()));
        } else if (key == "step") {
            golden.step = std::max(0.0, std::atof(value.c_str()));
        } else if (key == "focus") {
            golden.focus = std::atoi(value.c_str());
        } else if (key == "jd") {
            golden.jd = std::atof(value.c_str());
        } else if (key == "fields") {
            golden.fields = value == "1";
        } else if (key == "out") {
            golden.out = value;
        } else {
            LOG_WARN("Golden", "Unknown SOLARSYSTEM_GOLDEN key \"%s\"", key.c_str());
        }
    }
    return golden;
}

double GoldenStartJulianDate() {
    const char* spec = std::getenv("SOLARSYSTEM_GOLDEN");
    return spec && *spec ? ParseGoldenSpec(spec).jd : 0.0;
}
#endif
