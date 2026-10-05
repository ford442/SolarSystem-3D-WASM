#ifndef SOLARSYSTEM_KTX2_WRITER_H
#define SOLARSYSTEM_KTX2_WRITER_H

// Minimal KTX2 writer for the offline tools: one 2D level of VK_FORMAT_R16G16B16A16_SFLOAT
// (vkFormat 97), no supercompression, a basic Data Format Descriptor and optional
// key/value entries. Counterpart of ktx2_reader.h; GL-free so tests can round-trip it.
// Used by tools/atmosphere_lut_baker for the atmosphere LUTs in resource/atmosphere/.

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace ktx2 {

constexpr std::uint32_t kVkFormatR16G16B16A16Sfloat = 97;

/**
 * Encode width x height RGBA texels (row-major, bottom row first as GL uploads them) as
 * half floats. `rgba` holds 4 * width * height floats. Key/value entries are written
 * sorted by key, as the spec requires.
 */
std::vector<std::uint8_t> WriteRgba16f(std::uint32_t width, std::uint32_t height, const std::vector<float>& rgba,
                                       std::vector<std::pair<std::string, std::string>> keyValues);

/** IEEE 754 binary16 <-> binary32, round-to-nearest-even; exposed for the baker's --check. */
std::uint16_t FloatToHalf(float value);
float HalfToFloat(std::uint16_t half);

} // namespace ktx2

#endif // SOLARSYSTEM_KTX2_WRITER_H
