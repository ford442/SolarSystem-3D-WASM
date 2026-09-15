// Keyboard, mouse and touch input: the per-frame polled state (ProcessInput) and the
// GLFW event callbacks that toggle overlays, focus planets and drive the time controls.
#include "Application.h"
#include "JsBridge.h"
#include "SimState.h"
#include "WasmExports.h"
#include <algorithm>
#include <cmath>

using namespace std;

void Application::ProcessInput(GLFWwindow* window) {
    static float movementSpeed = _camera.GetMovementSpeed();

    if (_isFirstMouse) {
        _lastX = 0;
        _lastY = 0;
        _isFirstMouse = false;
    }

    float xPos = _lastX, yPos = _lastY;
    float shiftIncrease = 1.0f, yScroll = 0;

    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) {
        shiftIncrease = 4 * _camera.GetMovementSpeed();
    }
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, true);
    }
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
        StopMissionFollow();
        _camera.ProcessKeyboard(CameraVector::FORWARD, _deltaTime * shiftIncrease);
    }
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
        StopMissionFollow();
        _camera.ProcessKeyboard(CameraVector::BACKWARD, _deltaTime * shiftIncrease);
    }
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
        StopMissionFollow();
        _camera.ProcessKeyboard(CameraVector::LEFT, _deltaTime * shiftIncrease);
    }
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
        StopMissionFollow();
        _camera.ProcessKeyboard(CameraVector::RIGHT, _deltaTime * shiftIncrease);
    }
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) {
        StopMissionFollow();
        _camera.ProcessKeyboard(CameraVector::WORLD_UP, _deltaTime * shiftIncrease);
    }
    if (glfwGetKey(window, GLFW_KEY_C) == GLFW_PRESS) {
        StopMissionFollow();
        _camera.ProcessKeyboard(CameraVector::WORLD_DOWN, _deltaTime * shiftIncrease);
    }

    if (glfwGetKey(window, GLFW_KEY_PAGE_UP) == GLFW_PRESS) {
#ifdef SOLARSYSTEM_USE_SDL_MIXER
        SetMusicVolume(GetMusicVolume() + 0.05f);
#else
        _soundEngine->setSoundVolume(clamp(_soundEngine->getSoundVolume() + 0.01, 0.0, 1.0));
#endif
        NotifySettingsChanged("musicVolume");
    }
    if (glfwGetKey(window, GLFW_KEY_PAGE_DOWN) == GLFW_PRESS) {
#ifdef SOLARSYSTEM_USE_SDL_MIXER
        SetMusicVolume(GetMusicVolume() - 0.05f);
#else
        _soundEngine->setSoundVolume(clamp(_soundEngine->getSoundVolume() - 0.01, 0.0, 1.0));
#endif
        NotifySettingsChanged("musicVolume");
    }
    if (glfwGetKey(window, GLFW_KEY_1) == GLFW_PRESS) {
        movementSpeed = glm::clamp(movementSpeed + 0.01f, 0.0f, 150.f);
        _camera.SetMovementSpeed(movementSpeed);
    }
    if (glfwGetKey(window, GLFW_KEY_2) == GLFW_PRESS) {
        movementSpeed = glm::clamp(movementSpeed - 0.01f, 0.0f, 150.f);
        _camera.SetMovementSpeed(movementSpeed);
    }
    if (glfwGetKey(window, GLFW_KEY_3) == GLFW_PRESS) {
        _starExposure = glm::clamp(_starExposure + 0.1f, 0.0f, 20.f);
    }
    if (glfwGetKey(window, GLFW_KEY_4) == GLFW_PRESS) {
        _starExposure = glm::clamp(_starExposure - 0.1f, 0.0f, 20.f);
    }
    if (glfwGetKey(window, GLFW_KEY_5) == GLFW_PRESS) {
        _starGamma = glm::clamp(_starGamma + 0.01f, 0.0f, 2.f);
    }
    if (glfwGetKey(window, GLFW_KEY_6) == GLFW_PRESS) {
        _starGamma = glm::clamp(_starGamma - 0.01f, 0.0f, 2.f);
    }
    if (glfwGetKey(window, GLFW_KEY_7) == GLFW_PRESS) {
        _starTemperatureInKelvin = glm::clamp(_starTemperatureInKelvin + 15.0f, 800.f, 30000.f);
    }
    if (glfwGetKey(window, GLFW_KEY_8) == GLFW_PRESS) {
        _starTemperatureInKelvin = glm::clamp(_starTemperatureInKelvin - 15.0f, 800.f, 30000.f);
    }
    if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS) {
        xPos -= 1;
        float xOffset = xPos - _lastX;
        _lastX = xPos;
        _camera.ProcessMouseMovement(xOffset, 0);
    }
    if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) {
        xPos += 1;
        float xOffset = xPos - _lastX;
        _lastX = xPos;
        _camera.ProcessMouseMovement(xOffset, 0);
    }
    if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS) {
        yPos -= 1;
        float yOffset = _lastY - yPos;
        _lastY = yPos;
        _camera.ProcessMouseMovement(0, yOffset);
    }
    if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS) {
        yPos += 1;
        float yOffset = _lastY - yPos;
        _lastY = yPos;
        _camera.ProcessMouseMovement(0, yOffset);
    }
    if (glfwGetKey(window, GLFW_KEY_V) == GLFW_PRESS) {
        yScroll += 0.16f;
        _camera.ProcessMouseScroll(yScroll);
    }
    if (glfwGetKey(window, GLFW_KEY_B) == GLFW_PRESS) {
        yScroll -= 0.16f;
        _camera.ProcessMouseScroll(yScroll);
    }

#ifdef __EMSCRIPTEN__
    const float touchForward = GetTouchForward();
    const float touchRight = GetTouchRight();
    const float touchVertical = GetTouchVertical();
    if (std::abs(touchForward) > 0.01f || std::abs(touchRight) > 0.01f || std::abs(touchVertical) > 0.01f) {
        StopMissionFollow();
    }
    if (touchForward > 0.01f) {
        _camera.ProcessKeyboard(CameraVector::FORWARD, static_cast<float>(_deltaTime) * touchForward);
    } else if (touchForward < -0.01f) {
        _camera.ProcessKeyboard(CameraVector::BACKWARD, static_cast<float>(_deltaTime) * -touchForward);
    }
    if (touchRight > 0.01f) {
        _camera.ProcessKeyboard(CameraVector::RIGHT, static_cast<float>(_deltaTime) * touchRight);
    } else if (touchRight < -0.01f) {
        _camera.ProcessKeyboard(CameraVector::LEFT, static_cast<float>(_deltaTime) * -touchRight);
    }
    if (touchVertical > 0.01f) {
        _camera.ProcessKeyboard(CameraVector::WORLD_UP, static_cast<float>(_deltaTime) * touchVertical);
    } else if (touchVertical < -0.01f) {
        _camera.ProcessKeyboard(CameraVector::WORLD_DOWN, static_cast<float>(_deltaTime) * -touchVertical);
    }
#endif

    glfwSetCursorPos(window, _lastX, _lastY);
}

void Application::MouseCallback(GLFWwindow* window, double xPos, double yPos) {
    auto* app = static_cast<Application*>(glfwGetWindowUserPointer(window));
    if (!app) return;

    if (app->_isFirstMouse) {
        app->_lastX = static_cast<float>(xPos);
        app->_lastY = static_cast<float>(yPos);
        app->_isFirstMouse = false;
    }

    float xOffset = static_cast<float>(xPos) - app->_lastX;
    float yOffset = app->_lastY - static_cast<float>(yPos);

    app->_lastX = static_cast<float>(xPos);
    app->_lastY = static_cast<float>(yPos);

    app->_camera.ProcessMouseMovement(xOffset, yOffset);
}

void Application::ScrollCallback(GLFWwindow* window, double, double yOffset) {
    auto* app = static_cast<Application*>(glfwGetWindowUserPointer(window));
    if (!app) return;
    app->_camera.ProcessMouseScroll(static_cast<float>(yOffset));
}

void Application::KeyCallback(GLFWwindow* window, int key, int, int action, int) {
    auto* app = static_cast<Application*>(glfwGetWindowUserPointer(window));
    if (!app) return;

    if (key == GLFW_KEY_Z && action == GLFW_PRESS) {
        app->_isRenderPlanetStarDistances = !app->_isRenderPlanetStarDistances;
    }
    if (key == GLFW_KEY_X && action == GLFW_PRESS) {
        app->_isRenderSatelliteDistances = !app->_isRenderSatelliteDistances;
    }
    if (key == GLFW_KEY_TAB && action == GLFW_PRESS) {
        app->_isRenderHints = !app->_isRenderHints;
    }
    if (key == GLFW_KEY_F1 && action == GLFW_PRESS) {
        app->_isVertSyncEnabled = !app->_isVertSyncEnabled;
        VertSync(app->_isVertSyncEnabled);
    }

    if (action == GLFW_PRESS) {
        // Planet focus presets (smooth transition to live orbital positions)
        if (key == GLFW_KEY_F2) app->FocusPlanetByIndex(1);  // Mercury
        if (key == GLFW_KEY_F3) app->FocusPlanetByIndex(2);  // Venus
        if (key == GLFW_KEY_F4) app->FocusPlanetByIndex(3);  // Earth
        if (key == GLFW_KEY_F5) app->FocusPlanetByIndex(4);  // Mars
        if (key == GLFW_KEY_F6) app->FocusPlanetByIndex(5);  // Jupiter
        if (key == GLFW_KEY_F7) app->FocusPlanetByIndex(6);  // Saturn
        if (key == GLFW_KEY_F8) app->FocusPlanetByIndex(7);  // Uranus
        if (key == GLFW_KEY_F9) app->FocusPlanetByIndex(8);  // Neptune
        if (key == GLFW_KEY_F10) app->FocusPlanetByIndex(9); // Pluto
        if (key == GLFW_KEY_F11) app->FocusPlanetByIndex(0); // Sun

        // Time scale / pause / step
        if (key == GLFW_KEY_EQUAL || key == GLFW_KEY_KP_ADD) {
            gSimState->timeScale = glm::clamp(gSimState->timeScale * 2.0f, 0.01f, 10000.0f);
            NotifySettingsChanged("timeScale");
        }
        if (key == GLFW_KEY_MINUS || key == GLFW_KEY_KP_SUBTRACT) {
            gSimState->timeScale = glm::clamp(gSimState->timeScale / 2.0f, 0.01f, 10000.0f);
            NotifySettingsChanged("timeScale");
        }
        if (key == GLFW_KEY_P) {
            gSimState->timePaused = !gSimState->timePaused;
            NotifySettingsChanged("paused");
        }
        if (key == GLFW_KEY_M) {
            app->SetMagneticFieldsEnabled(!app->GetMagneticFieldsEnabled());
            NotifySettingsChanged("magneticFields");
            NotifySettingsChanged("magneticFieldMode");
        }
        if (key == GLFW_KEY_PERIOD) {
            gSimState->advanceStep = true;
        }
    }
}
