#ifndef SOLARSYSTEM_SYSTEMVISUALS_H
#define SOLARSYSTEM_SYSTEMVISUALS_H

#include "BodyCatalog.generated.h"

/**
 * Hand-tuned ring mesh numbers that are *not* in the planet catalog (rewriting rings as
 * JSON is a follow-up). The factory keys these by catalog body id when Entry.hasRings is
 * set. Atmospheres moved to the catalog: render.atmosphere → BodyCatalog::kAtmospheres.
 */
namespace SystemVisuals {

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
