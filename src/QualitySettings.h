#ifndef SOLARSYSTEM_QUALITY_SETTINGS_H
#define SOLARSYSTEM_QUALITY_SETTINGS_H

#include <cstdint>
#include <string>

enum class TextureLodTier : uint8_t {
    Low = 0,
    Mid = 1,
    High = 2
};

struct QualityTierSettings {
    uint16_t shadowResolution;
    int maxConcurrentTextureLoads;
    int requestedMsaaSamples;
    bool enableHdr;
    bool enableMagneticBloom;
    int magneticBloomPasses;
    int asteroidInstanceCount;
    // Kuiper-disk rocks (a second AsteroidField population, drawn in realistic scale only).
    int kuiperInstanceCount;
    TextureLodTier maxTextureLodTier;
    // LUT-based atmosphere (atmospherePbr.fs) on bodies whose catalog row has a `physical`
    // block; atmosphereSteps is the view-ray sample count. Off = the O'Neil shell.
    bool enablePbrAtmosphere;
    int atmosphereSteps;
    // Instanced corona slices (starCoronaVolume.*), perpendicular to the camera->Sun axis
    // (view-aligned inside the corona). Off = the flat billboard.
    bool enableVolumetricCorona;
    int coronaSlices;
    // How many of the brightest catalog stars Observe mode draws (the list is sorted by
    // magnitude, so this is a prefix). Planets, Sun and Moon are always drawn.
    int observeStarCount;
    // Web staged loading: how many planet systems nearest the start pose load right away
    // instead of waiting for the camera to approach them (see ActivateNearestPlanetSystems).
    int eagerPlanetSystems;
    // Height tiles on bodies flagged render.terrain (src/render/terrain/). A budget of 0 keeps
    // every body its sphere and never fetches a tile. The pixel error is the largest a tile's
    // quad may span on screen before it splits; the in-flight cap bounds tile downloads.
    int terrainBudgetMiB;
    float terrainPixelError;
    int terrainMaxInFlight;
    const char* name;
};

/**
 * Samples to ask the GL context for. A tier that renders through the HDR target gets none:
 * the colour pass draws into HDR's single-sampled texture, so default-framebuffer MSAA would
 * only resolve an image that is already resolved. Those tiers antialias in the tone-map
 * pass instead (hdr.fs). Native applies this with GLFW_SAMPLES; web mirrors it in
 * web/src/webglContext.ts, which owns the context attributes.
 */
int ContextMsaaSamples(const QualityTierSettings& settings);

/** What actually antialiases the final image. */
enum class AntialiasPath : uint8_t {
    None,
    ContextMsaa, // multisampled default framebuffer, drawn into directly (HDR off)
    HdrResolve,  // FXAA inside the tone-map pass
};
AntialiasPath ResolveAntialiasPath(bool hdrEnabled, int contextSamples);
const char* AntialiasPathName(AntialiasPath path);

std::string GetTexturePath(const std::string& lowRes, const std::string& highRes);
QualityTierSettings GetQualitySettings(int preset, bool mobile);
/**
 * What the quality tier allows the texture LOD controllers: the highest tier they may
 * stream, and the factor on a planet's upgrade distance (web Medium upgrades 1.5x closer).
 */
struct TextureLodPolicy {
    TextureLodTier maxTier = TextureLodTier::High;
    float planetThresholdScale = 1.0f;
};
TextureLodPolicy GetTextureLodPolicy(int preset, bool mobile);
const char* TextureLodTierName(TextureLodTier tier);
/** `contextSamples` is what GL_SAMPLES reported, not what the tier asked for. */
void LogQualityTier(const QualityTierSettings& settings, bool hdrEnabled, int shadowQuality, int contextSamples);

/**
 * Quality tier to build the GL context from, before any UI exists. Web reads
 * window.__solarSystemInit (set from the ?quality= query parameter); native reads the
 * SOLARSYSTEM_QUALITY environment variable ("low"/"medium"/"full" or 0/1/2). Defaults to
 * 2 ("full"). This is the only point where context MSAA can still be chosen — see ContextMsaaSamples.
 */
int ReadInitialQualityPreset();

#ifdef __EMSCRIPTEN__
bool ReadIsMobileWeb();
float ReadBackingStoreScale();
#endif

#endif // SOLARSYSTEM_QUALITY_SETTINGS_H
