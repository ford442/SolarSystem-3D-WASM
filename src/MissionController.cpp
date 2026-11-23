// Probe trajectories, Application's side: the catalog, focus and follow, and each probe's
// position for the frame. Renderer draws the paths (render/passes/OverlayPasses.cpp) from what
// Contribute puts in the snapshot.
#include "MissionController.h"
#include "FrameContext.h"
#include "IAppModule.h"
#include "JsBridge.h"
#include "ObserveState.h"
#include "SimState.h"
#include "Solar_System/OrbitLayout.h"
#include "Solar_System/StarSystemCatalog.h"
#include "astro/MissionCatalog.h"
#include "core/Camera.h"
#include "core/JobSystem.h"
#include "core/Log.h"
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

namespace {

constexpr const char* kMissionCatalogPath = "resource/missions/catalog.json";
constexpr double kFollowDistance = 45.0; // scene units from the probe

glm::vec3 FollowDirection() {
    return glm::normalize(glm::vec3(0.7f, 0.3f, 0.7f));
}

} // namespace

MissionController::MissionController(const ModuleContext& app, Camera& camera) : _app(app), _camera(camera) {}

void MissionController::StartCatalogJob() {
    // The JSON is preloaded (web .data, native disk), so it can be read now and parsed while
    // the core resources download; LoadCatalog takes the result.
    std::ifstream in(kMissionCatalogPath);
    if (!in) {
        return; // LoadCatalog reports it
    }
    std::ostringstream text;
    text << in.rdbuf();
    _catalogJob = Jobs::Submit([json = text.str()] {
        Parsed parsed;
        parsed.ok = MissionCatalog::LoadFromString(json, parsed.catalog, parsed.error);
        return parsed;
    });
}

void MissionController::LoadCatalog() {
    std::string error;
    bool ok = false;
    if (_catalogJob.Valid()) {
        Parsed parsed = _catalogJob.Get();
        ok = parsed.ok;
        _catalog = std::move(parsed.catalog);
        error = std::move(parsed.error);
    } else {
        ok = MissionCatalog::LoadFromFile(kMissionCatalogPath, _catalog, error);
    }
    if (!ok) {
        LOG_WARN("Missions", "%s", error.c_str());
        _catalog = {};
        return;
    }
    LOG_INFO("Missions", "Loaded %zu trajectory path(s) from catalog.json", _catalog.missions.size());
}

bool MissionController::Focus(int idx) {
    if (_app.observe.active) {
        return false; // the viewer stays on the ground in Observe
    }
    if (!StarSystemCatalog::IsSol()) {
        return false; // the probes fly in Sol
    }
    if (idx < 0 || idx >= Count()) {
        return false;
    }
    _focused = idx;
    _following = true;
    NotifyMissionFocused(idx);

    glm::dvec3 target(0.0);
    if (!SampleHeliocentric(idx, target)) {
        return true;
    }
    const glm::vec3 direction = FollowDirection();
    const glm::dvec3 cameraPos = target + glm::dvec3(direction) * kFollowDistance;
    const glm::vec3 lookDir = -direction;
    const float yaw = glm::degrees(std::atan2(lookDir.z, lookDir.x));
    const float pitch = glm::degrees(std::asin(glm::clamp(lookDir.y, -1.0f, 1.0f)));
    _camera.StartTransitionTo(cameraPos, yaw, pitch, 2.0f);
    return true;
}

void MissionController::Stop() {
    const bool wasFocused = _focused >= 0;
    _following = false;
    _focused = -1;
    if (wasFocused) {
        NotifyMissionFocused(-1);
    }
}

void MissionController::UpdateFollow() {
    if (!_following || _focused < 0 || _camera.IsTransitionActive()) {
        return;
    }
    glm::dvec3 target(0.0);
    if (!SampleHeliocentric(_focused, target)) {
        return;
    }
    const glm::vec3 direction = FollowDirection();
    const glm::vec3 lookDir = -direction;
    const float yaw = glm::degrees(std::atan2(lookDir.z, lookDir.x));
    const float pitch = glm::degrees(std::asin(glm::clamp(lookDir.y, -1.0f, 1.0f)));
    _camera.SetWorldPosition(target + glm::dvec3(direction) * kFollowDistance);
    _camera.SetYawPitch(yaw, pitch);
}

bool MissionController::SampleHeliocentric(int idx, glm::dvec3& outScene) const {
    if (idx < 0 || idx >= Count()) {
        return false;
    }
    glm::dvec3 au{0.0};
    if (!MissionCatalog::InterpolateAu(_catalog.missions[static_cast<std::size_t>(idx)], OrbitLayout::GetJulianDate(),
                                       au)) {
        return false;
    }
    outScene = OrbitLayout::HelioAuToSceneDouble(au);
    return true;
}

bool MissionController::SampleRender(int idx, glm::vec3& outScene) const {
    glm::dvec3 heliocentric{0.0};
    if (!SampleHeliocentric(idx, heliocentric)) {
        return false;
    }
    outScene = OrbitLayout::ToRenderFrame(heliocentric);
    return true;
}

std::string MissionController::CatalogJson() const {
    std::ostringstream ss;
    ss << '[';
    for (std::size_t i = 0; i < _catalog.missions.size(); ++i) {
        const auto& mission = _catalog.missions[i];
        if (i > 0) {
            ss << ',';
        }
        ss << R"({"id":")" << mission.id << R"(","name":")" << mission.name << R"(","index":)" << i << '}';
    }
    ss << ']';
    return ss.str();
}

std::string MissionController::FocusedJson() const {
    if (_focused < 0 || _focused >= Count()) {
        return R"({"valid":false})";
    }
    glm::dvec3 pos{0.0};
    if (!SampleHeliocentric(_focused, pos)) {
        return R"({"valid":false})";
    }
    const auto& mission = _catalog.missions[static_cast<std::size_t>(_focused)];
    char buffer[384];
    std::snprintf(buffer, sizeof(buffer), R"({"valid":true,"id":"%s","index":%d,"x":%.4f,"y":%.4f,"z":%.4f})",
                  mission.id.c_str(), _focused, pos.x, pos.y, pos.z);
    return buffer;
}

void MissionController::Update(SimState& /*sim*/) {
    _probes.resize(StarSystemCatalog::IsSol() ? _catalog.missions.size() : 0);
    for (std::size_t i = 0; i < _probes.size(); ++i) {
        glm::vec3 probe(0.0f);
        _probes[i] = SampleRender(static_cast<int>(i), probe) ? std::optional(probe) : std::nullopt;
    }
}

void MissionController::Contribute(SceneSnapshot& scene) {
    static const MissionCatalog::Catalog kNoMissions;
    scene.missions = StarSystemCatalog::IsSol() ? &_catalog : &kNoMissions;
    scene.focusedMission = _focused;
    scene.missionProbes = &_probes;
}
