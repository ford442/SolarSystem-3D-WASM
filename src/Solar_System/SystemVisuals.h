#ifndef SOLARSYSTEM_SYSTEMVISUALS_H
#define SOLARSYSTEM_SYSTEMVISUALS_H

#include "BodyCatalog.generated.h"
#include <glm/glm.hpp>

/**
 * Hand-tuned atmosphere and ring numbers that are *not* in the planet catalog
 * (rewriting atmospheres/rings as JSON is a follow-up). The factory keys these
 * by catalog body id when Entry.hasAtmosphere / hasRings is set.
 */
namespace SystemVisuals {

struct AtmosphereSpec {
    const char* bodyId;
    float scaleFactor;
    glm::vec3 color;
    bool innerRadiusMinusEpsilon;
    float outerRadius;
    glm::vec3 mieTint;
    float hScaleFactor;
};

inline constexpr AtmosphereSpec kAtmospheres[] = {
    {"venus", 1.1f, {203.0f / 255.0f, 158.0f / 255.0f, 69.0f / 255.0f}, true, 1.995f, {1.0f, 1.0f, 1.0f}, 6.0f},
    {"earth", 1.1f, {0.3f, 0.7f, 1.0f}, true, 2.1f, {1.0f, 1.0f, 1.0f}, 6.0f},
    {"mars", 0.583f, {0.976f, 0.302f, 0.208f}, true, 1.113f, {1.0f, 1.0f, 1.0f}, 6.0f},
    {"jupiter", 11.4f, {153.0f / 255.0f, 139.0f / 255.0f, 120.0f / 255.0f}, true, 23.35f, {1.0f, 1.0f, 1.0f}, 26.0f},
    {"saturn", 9.34f, {84.0f / 255.0f, 132.0f / 255.0f, 176.0f / 255.0f}, true, 18.6f, {1.0f, 1.0f, 1.0f}, 27.0f},
    {"uranus", 4.0f, {45.0f / 255.0f, 101.0f / 255.0f, 114.0f / 255.0f}, true, 8.1f, {1.0f, 1.0f, 1.0f}, 24.0f},
    {"neptune", 3.9f, {62.0f / 255.0f, 92.0f / 255.0f, 169.0f / 255.0f}, true, 7.9f, {1.0f, 1.0f, 1.0f}, 23.0f},
    {"pluto", 0.45f, {92.0f / 255.0f, 120.0f / 255.0f, 141.0f / 255.0f}, false, 1.0f,
     {35.0f / 255.0f, 52.0f / 255.0f, 220.0f / 255.0f}, 16.0f},
    {"titan", 0.504136f, {40.0f / 255.0f, 33.0f / 255.0f, 72.0f / 255.0f}, true, 0.8429210f,
     {0.36862745f, 0.0666667f, 0.0196078f}, 4.8f},
};

struct RingSpec {
    const char* bodyId;
    const char* modelPath;
    float innerRadius;
    float outerRadius;
    const char* textureId;
};

inline constexpr RingSpec kRings[] = {
    {"saturn", "resource/models/saturn_ring.obj", 22.0f, 43.7f, "Saturn_Rings"},
    {"uranus", "resource/models/uranus_ring.obj", 12.6f, 16.0f, "Uranus_Rings"},
};

inline constexpr const AtmosphereSpec* FindAtmosphere(const char* bodyId) {
    for (const AtmosphereSpec& spec : kAtmospheres) {
        if (BodyCatalog::StrEq(spec.bodyId, bodyId)) {
            return &spec;
        }
    }
    return nullptr;
}

inline constexpr const RingSpec* FindRing(const char* bodyId) {
    for (const RingSpec& spec : kRings) {
        if (BodyCatalog::StrEq(spec.bodyId, bodyId)) {
            return &spec;
        }
    }
    return nullptr;
}

} // namespace SystemVisuals

#endif //SOLARSYSTEM_SYSTEMVISUALS_H
