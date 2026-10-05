#include "ktx2_writer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace ktx2 {
namespace {

constexpr std::uint8_t kIdentifier[12] = {
    0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32, 0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A
};

void PutU32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>((value >> (8 * i)) & 0xFF));
    }
}

void PutU64(std::vector<std::uint8_t>& out, std::uint64_t value) {
    PutU32(out, static_cast<std::uint32_t>(value & 0xFFFFFFFFu));
    PutU32(out, static_cast<std::uint32_t>(value >> 32));
}

void SetU32(std::vector<std::uint8_t>& out, std::size_t at, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        out[at + i] = static_cast<std::uint8_t>((value >> (8 * i)) & 0xFF);
    }
}

void SetU64(std::vector<std::uint8_t>& out, std::size_t at, std::uint64_t value) {
    SetU32(out, at, static_cast<std::uint32_t>(value & 0xFFFFFFFFu));
    SetU32(out, at + 4, static_cast<std::uint32_t>(value >> 32));
}

void PadTo(std::vector<std::uint8_t>& out, std::size_t alignment) {
    while (out.size() % alignment != 0) {
        out.push_back(0);
    }
}

// Basic DFD for RGBA 16-bit float, linear, BT.709 primaries (KDF 1.3 §5).
void PutDataFormatDescriptor(std::vector<std::uint8_t>& out) {
    constexpr std::uint32_t kSamples = 4;
    constexpr std::uint32_t kBlockSize = 24 + 16 * kSamples;
    PutU32(out, 4 + kBlockSize);            // dfdTotalSize
    PutU32(out, 0);                         // vendorId 0 (Khronos) | descriptorType 0 (basic)
    PutU32(out, 2u | (kBlockSize << 16));   // versionNumber 2 | descriptorBlockSize
    // colorModel RGBSDA (1) | primaries BT709 (1) | transfer LINEAR (1) | flags straight alpha (0)
    PutU32(out, 1u | (1u << 8) | (1u << 16));
    PutU32(out, 0);                         // texelBlockDimension0..3 = 1x1x1x1 (stored minus one)
    PutU32(out, 8);                         // bytesPlane0 = 8
    PutU32(out, 0);                         // bytesPlane4..7
    const std::uint32_t channels[kSamples] = {0, 1, 2, 15}; // R, G, B, A
    for (std::uint32_t i = 0; i < kSamples; ++i) {
        constexpr std::uint32_t kQualifierFloatSigned = 0x80 | 0x40;
        const std::uint32_t bitOffset = 16 * i;
        const std::uint32_t bitLength = 16 - 1;
        PutU32(out, bitOffset | (bitLength << 16) | ((channels[i] | kQualifierFloatSigned) << 24));
        PutU32(out, 0);          // samplePosition0..3
        PutU32(out, 0xBF800000); // sampleLower = -1.0f
        PutU32(out, 0x3F800000); // sampleUpper = 1.0f
    }
}

} // namespace

std::uint16_t FloatToHalf(float value) {
    std::uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    const std::uint32_t sign = (bits >> 16) & 0x8000u;
    const std::uint32_t exponent = (bits >> 23) & 0xFFu;
    std::uint32_t mantissa = bits & 0x7FFFFFu;

    if (exponent == 0xFF) { // Inf / NaN
        return static_cast<std::uint16_t>(sign | 0x7C00u | (mantissa ? 0x200u : 0u));
    }
    const int e = static_cast<int>(exponent) - 127 + 15;
    if (e >= 0x1F) { // overflow -> Inf
        return static_cast<std::uint16_t>(sign | 0x7C00u);
    }
    if (e <= 0) { // subnormal or zero
        if (e < -10) {
            return static_cast<std::uint16_t>(sign);
        }
        mantissa |= 0x800000u;
        const int shift = 14 - e;
        std::uint32_t half = mantissa >> shift;
        const std::uint32_t remainder = mantissa & ((1u << shift) - 1u);
        const std::uint32_t halfway = 1u << (shift - 1);
        if (remainder > halfway || (remainder == halfway && (half & 1u))) {
            ++half;
        }
        return static_cast<std::uint16_t>(sign | half);
    }
    std::uint32_t half = (static_cast<std::uint32_t>(e) << 10) | (mantissa >> 13);
    const std::uint32_t remainder = mantissa & 0x1FFFu;
    if (remainder > 0x1000u || (remainder == 0x1000u && (half & 1u))) {
        ++half; // may carry into the exponent, which is the correct rounding
    }
    return static_cast<std::uint16_t>(sign | half);
}

float HalfToFloat(std::uint16_t half) {
    const std::uint32_t sign = (static_cast<std::uint32_t>(half) & 0x8000u) << 16;
    const std::uint32_t exponent = (half >> 10) & 0x1Fu;
    const std::uint32_t mantissa = half & 0x3FFu;
    float magnitude;
    if (exponent == 0) {
        magnitude = std::ldexp(static_cast<float>(mantissa), -24);
    } else if (exponent == 0x1F) {
        magnitude = mantissa ? std::nanf("") : INFINITY;
    } else {
        magnitude = std::ldexp(static_cast<float>(mantissa | 0x400u), static_cast<int>(exponent) - 25);
    }
    std::uint32_t bits;
    std::memcpy(&bits, &magnitude, sizeof(bits));
    bits |= sign;
    float result;
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}

std::vector<std::uint8_t> WriteRgba16f(std::uint32_t width, std::uint32_t height, const std::vector<float>& rgba,
                                       std::vector<std::pair<std::string, std::string>> keyValues) {
    if (width == 0 || height == 0 || rgba.size() != static_cast<std::size_t>(width) * height * 4) {
        throw std::invalid_argument("WriteRgba16f: rgba must hold 4 * width * height floats");
    }
    std::sort(keyValues.begin(), keyValues.end());

    std::vector<std::uint8_t> out(kIdentifier, kIdentifier + sizeof(kIdentifier));
    PutU32(out, kVkFormatR16G16B16A16Sfloat);
    PutU32(out, 2);      // typeSize
    PutU32(out, width);
    PutU32(out, height);
    PutU32(out, 0);      // pixelDepth
    PutU32(out, 0);      // layerCount
    PutU32(out, 1);      // faceCount
    PutU32(out, 1);      // levelCount
    PutU32(out, 0);      // supercompressionScheme
    const std::size_t indexAt = out.size();
    for (int i = 0; i < 4; ++i) {
        PutU32(out, 0);  // dfd offset/length, kvd offset/length (patched below)
    }
    PutU64(out, 0);      // sgdByteOffset
    PutU64(out, 0);      // sgdByteLength
    const std::size_t levelIndexAt = out.size();
    PutU64(out, 0);      // level 0 byteOffset (patched)
    PutU64(out, 0);      // level 0 byteLength
    PutU64(out, 0);      // level 0 uncompressedByteLength

    const std::size_t dfdAt = out.size();
    PutDataFormatDescriptor(out);
    SetU32(out, indexAt + 0, static_cast<std::uint32_t>(dfdAt));
    SetU32(out, indexAt + 4, static_cast<std::uint32_t>(out.size() - dfdAt));

    const std::size_t kvdAt = out.size();
    for (const auto& [key, value] : keyValues) {
        const std::uint32_t length = static_cast<std::uint32_t>(key.size() + 1 + value.size() + 1);
        PutU32(out, length);
        out.insert(out.end(), key.begin(), key.end());
        out.push_back(0);
        out.insert(out.end(), value.begin(), value.end());
        out.push_back(0);
        PadTo(out, 4);
    }
    if (out.size() > kvdAt) {
        SetU32(out, indexAt + 8, static_cast<std::uint32_t>(kvdAt));
        SetU32(out, indexAt + 12, static_cast<std::uint32_t>(out.size() - kvdAt));
    }

    PadTo(out, 8); // lcm(texel size 8, 4)
    const std::size_t levelAt = out.size();
    for (const float component : rgba) {
        const std::uint16_t half = FloatToHalf(component);
        out.push_back(static_cast<std::uint8_t>(half & 0xFF));
        out.push_back(static_cast<std::uint8_t>(half >> 8));
    }
    const std::uint64_t levelLength = out.size() - levelAt;
    SetU64(out, levelIndexAt + 0, levelAt);
    SetU64(out, levelIndexAt + 8, levelLength);
    SetU64(out, levelIndexAt + 16, levelLength);
    return out;
}

} // namespace ktx2
