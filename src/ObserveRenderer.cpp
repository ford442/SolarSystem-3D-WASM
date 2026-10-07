// Observe-mode drawing on the Renderer side: view setup for the ObserveSky pass and the
// text overlay (compass, body names, readout). The sky itself is Solar_System/ObserveSky.cpp.
#include "Application.h"
#include "Auxiliary_Modules/SeatedView.h"
#include "Auxiliary_Modules/Observer.h"
#include "SimState.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>

namespace {

constexpr float kSkyNear = 0.1f;
constexpr float kSkyFar = 10.0f;
constexpr double kDegToRad = 3.14159265358979323846 / 180.0;

std::wstring widen(const std::string& ascii) {
    return std::wstring(ascii.begin(), ascii.end());
}

std::wstring formatCoordinate(double value, wchar_t positive, wchar_t negative) {
    std::wostringstream out;
    out << std::fixed << std::setprecision(2) << std::fabs(value) << L' ' << (value < 0.0 ? negative : positive);
    return out.str();
}

} // namespace

void Renderer::RenderObserveSky() {
    if (!observeSky) {
        return;
    }

    ObserveSky::Frame frame;
#ifdef __EMSCRIPTEN__
    if (_app._xr.active && _app._xr.currentEye >= 0 && _app._xr.currentEye < _app._xr.eyeCount) {
        const auto& eye = _app._xr.eyes[_app._xr.currentEye];
        // Seated: the headset gives pitch/roll/yaw within the room; the camera's yaw (changed by a
        // snap turn) is the sky heading the room's forward faces. Pitch is the viewer's own head.
        frame.viewRot = SeatedView::ViewRotation(glm::mat3(eye.view), _app._camera.GetYaw() + 90.0f);
        frame.projection = eye.projection;
        frame.viewportWidth = eye.viewportWidth;
        frame.viewportHeight = eye.viewportHeight;
    } else
#endif
    {
        const Camera& camera = _app._camera;
        frame.viewRot = glm::mat3(camera.GetViewMatrix()); // rotation only: the sky is at infinity
        frame.projection = glm::perspective(glm::radians(camera.GetZoom()), camera.GetAspect(), kSkyNear, kSkyFar);
        frame.viewportWidth = _app._displayWidth;
        frame.viewportHeight = _app._displayHeight;
    }
    _observeFrame = frame;

    observeSky->Render(_app.CurrentObserveSky(), frame, _app.ObserveMoonTexture());

#ifdef __EMSCRIPTEN__
    if (_app._xr.active) {
        // The 2D HUD cannot go in a headset, but the labels are world-anchored: draw them per eye,
        // grown with the eye's resolution so they stay legible.
        const float textScale = glm::clamp(static_cast<float>(frame.viewportHeight) / 720.0f, 1.0f, 2.5f);
        RenderObserveLabels(static_cast<float>(frame.viewportWidth), static_cast<float>(frame.viewportHeight), textScale);
    }
#endif
}

void Renderer::RenderObserveLabels(float width, float height, float textScale) {
    if (!textRenderer || !mainTextShader) {
        return;
    }

    const Observer::Sky& sky = _app.CurrentObserveSky();

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    mainTextShader->Use();
    mainTextShader->SetMat4("projection", glm::ortho(0.0f, width, 0.0f, height));
    mainTextShader->SetBool("is3D", false);

    // Compass points on the horizon.
    struct Cardinal { const wchar_t* label; glm::vec3 dir; };
    const Cardinal cardinals[] = {
        {L"N", {0.0f, 0.0f, -1.0f}}, {L"E", {1.0f, 0.0f, 0.0f}},
        {L"S", {0.0f, 0.0f, 1.0f}},  {L"W", {-1.0f, 0.0f, 0.0f}},
    };
    const glm::vec3 compassColor(0.95f, 0.80f, 0.45f);
    for (const Cardinal& cardinal : cardinals) {
        glm::vec2 px;
        const glm::vec3 raised = glm::normalize(cardinal.dir + glm::vec3(0.0f, 0.035f, 0.0f));
        if (ObserveSky::ProjectToPixels(raised, _observeFrame, px) && px.x > 0.0f && px.x < width &&
            px.y > 0.0f && px.y < height) {
            textRenderer->Render(*mainTextShader, std::wstring(cardinal.label), px.x - 8.0f * textScale, px.y, 0.7f * textScale, compassColor);
        }
    }

    // Body names next to whatever is actually visible.
    const float limit = ObserveSky::EffectiveLimitingMagnitude(sky);
    const glm::vec3 labelColor(0.80f, 0.88f, 1.0f);
    for (int slot = 0; slot < Observer::kSkyBodyCount; ++slot) {
        const Observer::SkyBody& body = sky.bodies[slot];
        if (body.altDeg < -0.5) {
            continue;
        }
        if (slot >= Observer::kSkyMercury && static_cast<float>(body.magnitude) > limit + 0.5f) {
            continue;
        }
        glm::vec2 px;
        const glm::vec3 dir(static_cast<float>(body.dir[0]), static_cast<float>(body.dir[1]),
                            static_cast<float>(body.dir[2]));
        if (ObserveSky::ProjectToPixels(dir, _observeFrame, px) && px.x > 0.0f && px.x < width &&
            px.y > 0.0f && px.y < height) {
            textRenderer->Render(*mainTextShader, widen(body.name), px.x + 12.0f * textScale, px.y + 12.0f * textScale, 0.35f * textScale, labelColor);
        }
    }

    // Names of the brightest catalog stars that are actually drawn and above the sky glow.
    if (observeSky) {
        const StarCatalog::Catalog& catalog = observeSky->Stars();
        const int drawn = observeSky->DrawnStarCount();
        const Observer::Mat3 toHorizon = Observer::HorizonFromEquatorialJ2000(sky.julianDate, sky.site);
        const glm::vec3 starLabelColor(0.62f, 0.72f, 0.92f);
        constexpr float kStarLabelMaxMagnitude = 1.9f;
        for (const auto& [index, name] : catalog.names) {
            if (index >= drawn) {
                continue;
            }
            const StarCatalog::Star& star = catalog.stars[static_cast<size_t>(index)];
            if (star.vmag > kStarLabelMaxMagnitude || star.vmag > limit - 0.5f) {
                continue;
            }
            const double ra = star.raDeg * kDegToRad;
            const double dec = star.decDeg * kDegToRad;
            const double equatorial[3] = {std::cos(dec) * std::cos(ra), std::cos(dec) * std::sin(ra), std::sin(dec)};
            double horizon[3];
            toHorizon.Apply(equatorial, horizon);
            if (horizon[1] < 0.0) {
                continue;
            }
            glm::vec2 px;
            const glm::vec3 dir(static_cast<float>(horizon[0]), static_cast<float>(horizon[1]),
                                static_cast<float>(horizon[2]));
            if (ObserveSky::ProjectToPixels(dir, _observeFrame, px) && px.x > 0.0f && px.x < width &&
                px.y > 0.0f && px.y < height) {
                textRenderer->Render(*mainTextShader, widen(name), px.x + 8.0f * textScale, px.y + 8.0f * textScale, 0.3f * textScale, starLabelColor);
            }
        }
    }

    glDisable(GL_BLEND);
}

void Renderer::RenderObserveHud() {
    if (!textRenderer || !mainTextShader) {
        return;
    }

    const float width = static_cast<float>(_app._displayWidth);
    const float height = static_cast<float>(_app._displayHeight);
    const Observer::Sky& sky = _app.CurrentObserveSky();
    const ObserveState& observe = _app.GetObserveState();

    RenderObserveLabels(width, height, 1.0f);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    // Readout.
    const glm::vec3 textColor(0.98431f, 0.80784f, 0.69412f);
    {
        std::wostringstream line;
        line << L"Observe  " << formatCoordinate(observe.site.latDeg, L'N', L'S') << L"  "
             << formatCoordinate(observe.site.lonDeg, L'E', L'W') << L"  UTC "
             << widen(Observer::FormatUtc(OrbitLayout::GetJulianDate())) << L"  ";
        if (gSimState->timePaused) {
            line << L"paused";
        } else {
            line << L"x" << static_cast<long long>(observe.timeRate);
        }
        textRenderer->Render(*mainTextShader, line.str(), 0.01f * width, 0.95f * height, 0.35f, textColor);
    }
    {
        const Observer::SkyBody& sun = sky.bodies[Observer::kSkySun];
        const Observer::SkyBody& moon = sky.bodies[Observer::kSkyMoon];
        std::wostringstream line;
        line << std::fixed << std::setprecision(0) << L"Az " << std::fmod(_app._camera.GetYaw() + 450.0f, 360.0f)
             << L"  Alt " << _app._camera.GetPitch() << L"  FOV " << _app._camera.GetZoom()
             << L"   Sun " << std::setprecision(1) << sun.altDeg << L"  Moon " << moon.altDeg << L" ("
             << std::setprecision(0) << moon.illuminatedFraction * 100.0 << L"% lit)";
        if (sky.sunCoverage > 0.001) {
            line << (sky.sunCoverage >= 0.9999 ? std::wstring(L"   TOTAL ECLIPSE")
                                                : std::wstring(L"   Eclipse ") + std::to_wstring(static_cast<int>(sky.sunCoverage * 100.0)) + L"%");
        }
        textRenderer->Render(*mainTextShader, line.str(), 0.01f * width, 0.925f * height, 0.35f, textColor);
    }
    textRenderer->Render(*mainTextShader, std::wstring(L"Visualization, not an almanac.  (O: back to Explore)"),
                         0.01f * width, 0.02f * height, 0.35f, textColor);

    glDisable(GL_BLEND);
}
