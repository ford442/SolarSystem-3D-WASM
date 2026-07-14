#include "OrbitLayout.h"

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

namespace OrbitLayout {
namespace {

struct BodyData {
    glm::vec3 compressedOffset;
    float auDistance;
};

constexpr float kAuToSceneUnits = 1900.0f; // 1 AU ≈ Earth's compressed orbit radius

constexpr BodyData kBodies[] = {
    {glm::vec3(0.0f), 0.0f},                        // Sun
    {glm::vec3(1500.0f, 0.0f, 350.0f), 0.387f},     // Mercury
    {glm::vec3(1125.0f, 0.0f, -1340.0f), 0.723f},   // Venus
    {glm::vec3(1900.0f, 0.0f, 0.0f), 1.0f},         // Earth
    {glm::vec3(-1732.0f, 0.0f, 1000.0f), 1.524f},   // Mars
    {glm::vec3(1350.0f, 0.0f, 1737.0f), 5.203f},    // Jupiter
    {glm::vec3(0.0f, -100.0f, 2450.0f), 9.537f},    // Saturn
    {glm::vec3(0.0f, 0.0f, -2650.0f), 19.191f},     // Uranus
    {glm::vec3(-2900.0f, 0.0f, 0.0f), 30.069f},     // Neptune
    {glm::vec3(2800.0f, 0.0f, 1757.73f), 39.482f},  // Pluto
};

ScaleMode g_scaleMode = ScaleMode::Compressed;

int bodyIndex(Body body) {
    const int idx = static_cast<int>(body);
    return std::clamp(idx, 0, static_cast<int>(sizeof(kBodies) / sizeof(kBodies[0])) - 1);
}

} // namespace

void SetScaleMode(ScaleMode mode) {
    g_scaleMode = mode;
}

ScaleMode GetScaleMode() {
    return g_scaleMode;
}

glm::vec3 GetCompressedOffset(Body body) {
    return kBodies[bodyIndex(body)].compressedOffset;
}

float GetAuDistance(Body body) {
    return kBodies[bodyIndex(body)].auDistance;
}

glm::vec3 GetOffset(Body body) {
    const glm::vec3 compressed = GetCompressedOffset(body);
    if (g_scaleMode == ScaleMode::Compressed || body == Body::Sun) {
        return compressed;
    }

    const float au = GetAuDistance(body);
    const float targetLength = au * kAuToSceneUnits;
    const float compressedLength = glm::length(compressed);
    if (compressedLength < 0.001f || targetLength < 0.001f) {
        return compressed;
    }
    return glm::normalize(compressed) * targetLength;
}

float GetSceneDistance(Body body) {
    return glm::length(GetOffset(body));
}

Body BodyFromName(const std::string& name) {
    if (name == "Mercury") return Body::Mercury;
    if (name == "Venus") return Body::Venus;
    if (name == "Earth") return Body::Earth;
    if (name == "Mars") return Body::Mars;
    if (name == "Jupiter") return Body::Jupiter;
    if (name == "Saturn") return Body::Saturn;
    if (name == "Uranus") return Body::Uranus;
    if (name == "Neptune") return Body::Neptune;
    if (name == "Pluto") return Body::Pluto;
    return Body::Sun;
}

} // namespace OrbitLayout
