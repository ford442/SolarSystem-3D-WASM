#ifndef SOLARSYSTEM_FLOAT_LUT_TEXTURE_H
#define SOLARSYSTEM_FLOAT_LUT_TEXTURE_H

#include <GL/glew.h>

#include <optional>
#include <string>

/**
 * Loads a baked RGBA16F lookup table (one-level KTX2, vkFormat 97) as a clamped,
 * linearly filtered, mip-less 2D texture — the atmosphere LUTs in resource/atmosphere/.
 *
 * Deliberately not TextureImage2D: that one throws on native and swaps in a checkerboard on
 * web, and both are wrong for a LUT whose absence should quietly select the fallback
 * shader. Sampling RGBA16F with LINEAR filtering is core WebGL 2 / ES 3.0, so no
 * capability check is needed (only rendering into float targets needs
 * EXT_color_buffer_float). Never throws; logs to stdout and returns nullopt instead.
 */
struct FloatLut {
    GLuint texture = 0;
    std::string paramsHash;  // KTX2 key/value written by the baker, "" if absent
    int mappingVersion = -1;
};

std::optional<FloatLut> LoadRgba16fLut(const std::string& path, int expectedWidth, int expectedHeight);

#endif // SOLARSYSTEM_FLOAT_LUT_TEXTURE_H
