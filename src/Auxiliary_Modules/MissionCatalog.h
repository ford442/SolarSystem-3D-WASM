#ifndef SOLARSYSTEM_MISSIONCATALOG_H
#define SOLARSYSTEM_MISSIONCATALOG_H

#include <glm/vec3.hpp>
#include <string>
#include <vector>

/**
 * Sampled heliocentric mission trajectories (Voyager, etc.). Positions are stored in
 * scene-axis AU — the same Y-up convention as OrbitLayout::GetOffset — and mapped to
 * scene units at draw time so compressed/realistic scale both work.
 *
 * JSON is baked offline (`scripts/generate-mission-samples.mjs`). No SPICE, no network.
 */
namespace MissionCatalog {

struct Sample {
    double julianDate = 0.0;
    glm::vec3 au{0.0f}; // scene-axis AU
};

struct Mission {
    std::string id;
    std::string name;
    glm::vec3 color{1.0f, 0.78f, 0.22f};
    std::vector<Sample> samples;
};

struct Catalog {
    int version = 0;
    std::vector<Mission> missions;
};

/** Parse a catalog JSON file. Returns false and sets errorOut on failure. */
bool LoadFromFile(const std::string& path, Catalog& out, std::string& errorOut);

/** Parse a catalog JSON string. Returns false and sets errorOut on failure. */
bool LoadFromString(const std::string& source, Catalog& out, std::string& errorOut);

/** Index of `id`, or -1. */
int IndexById(const Catalog& catalog, const std::string& id);

/**
 * Linear interpolate the mission's AU sample at `julianDate`. Clamps to the first/last
 * sample when the epoch is outside the recorded span. Returns false when the mission
 * has no samples.
 */
bool InterpolateAu(const Mission& mission, double julianDate, glm::vec3& outAu);

/**
 * Downsample a mission polyline for a quality preset (0=low stride 4, 1=medium stride 2,
 * 2=full stride 1). Always keeps the first and last sample.
 */
std::vector<glm::vec3> DownsampledAu(const Mission& mission, int qualityPreset);

int SampleStrideForQuality(int qualityPreset);

} // namespace MissionCatalog

#endif // SOLARSYSTEM_MISSIONCATALOG_H
