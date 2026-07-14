#pragma once

#include <functional>
#include <string>
#include <vector>

#include <glm/vec3.hpp>

struct PlanetSystemManifest;

namespace PlanetManifestLoader {

using InitFuncFactory = std::function<std::function<void()>(const std::string& initTag)>;

// Reads planet_manifest.json and builds staged-loading manifests.
// proxyPosition values in JSON are offsets added to sunPosition.
// Returns an empty vector and sets errorOut on parse or binding failure.
std::vector<PlanetSystemManifest> LoadManifests(
    const std::string& path,
    glm::vec3 sunPosition,
    const InitFuncFactory& makeInitFunc,
    int& outVersion,
    std::string& errorOut);

} // namespace PlanetManifestLoader
