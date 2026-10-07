#ifndef SOLARSYSTEM_STARCATALOG_H
#define SOLARSYSTEM_STARCATALOG_H

#include <string>
#include <unordered_map>
#include <vector>

/**
 * The fixed-star field Observe mode draws: ~5000 stars from the Yale Bright Star Catalogue,
 * baked into resource/sky/bright_stars.json by scripts/generate-star-catalog.mjs.
 *
 * GL-free so it is unit-tested. Stars are J2000 equatorial and sorted brightest first, which
 * is what lets a quality tier draw a prefix of the list. Proper motion is not modelled.
 */
namespace StarCatalog {

struct Star {
    float raDeg = 0.0f;   // J2000 right ascension, [0, 360)
    float decDeg = 0.0f;  // J2000 declination
    float vmag = 0.0f;    // visual magnitude
    float bv = 0.6f;      // B-V colour index
};

struct Catalog {
    std::vector<Star> stars;                          // sorted by vmag ascending
    std::unordered_map<int, std::string> names;       // index into `stars` → common name
};

/** Parse the generator's JSON. False (and an empty catalog) for anything malformed. */
bool Parse(const std::string& json, Catalog& out, std::string& errorOut);

bool LoadFromFile(const std::string& path, Catalog& out, std::string& errorOut);

/** Approximate sRGB colour (0–1) of a star from B-V, softened toward white for display. */
void ColorFromBV(float bv, float rgbOut[3]);

} // namespace StarCatalog

#endif // SOLARSYSTEM_STARCATALOG_H
