// WebXR session state (web only): the per-eye viewports and matrices JS hands over
// each XR frame, and the stereo render pass that consumes them. See XrState.h for the
// state layout and WasmExports.cpp for the JS-facing entry points.
#ifdef __EMSCRIPTEN__
#include "Application.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <glm/gtc/type_ptr.hpp>

void Application::SetXrActive(bool active) {
    _xr.active = active;
    _xr.currentEye = 0;
    if (!active) {
        _xr.eyeCount = 0;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, _displayWidth, _displayHeight);
        glDisable(GL_SCISSOR_TEST);
    }
    std::cout << "[WebXR] session " << (active ? "active" : "inactive") << std::endl;
}

void Application::SetXrEyeCount(int count) {
    _xr.eyeCount = std::clamp(count, 0, 2);
}

void Application::SetXrEyeViewport(int eye, int x, int y, int width, int height) {
    if (eye < 0 || eye > 1) {
        return;
    }
    auto& e = _xr.eyes[eye];
    e.viewportX = x;
    e.viewportY = y;
    e.viewportWidth = std::max(width, 1);
    e.viewportHeight = std::max(height, 1);
}

float* Application::GetXrMatrixScratch() {
    return _xr.matrixScratch;
}

void Application::CommitXrEyeMatrices(int eye) {
    if (eye < 0 || eye > 1) {
        return;
    }
    auto& e = _xr.eyes[eye];
    std::memcpy(glm::value_ptr(e.view), _xr.matrixScratch, 16 * sizeof(float));
    std::memcpy(glm::value_ptr(e.projection), _xr.matrixScratch + 16, 16 * sizeof(float));
}

void Application::RenderXrStereoFrame() {
    // JS has already bound the XRWebGLLayer framebuffer and cleared it.
    const int eyes = std::clamp(_xr.eyeCount, 0, 2);
    for (int eye = 0; eye < eyes; ++eye) {
        _xr.currentEye = eye;
        const auto& e = _xr.eyes[eye];
        glViewport(e.viewportX, e.viewportY, e.viewportWidth, e.viewportHeight);
        glEnable(GL_SCISSOR_TEST);
        glScissor(e.viewportX, e.viewportY, e.viewportWidth, e.viewportHeight);
        glClear(GL_DEPTH_BUFFER_BIT);
        glDisable(GL_SCISSOR_TEST);
        RenderFrameContent();
    }
    _xr.currentEye = 0;
}
#endif
