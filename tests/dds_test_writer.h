#ifndef SOLARSYSTEM_TESTS_DDS_TEST_WRITER_H
#define SOLARSYSTEM_TESTS_DDS_TEST_WRITER_H
// Writes small DDS files for the decode/upload tests: every placeholder in resource/ is a 4x4
// RGBA, which exercises neither the DXT paths nor a mip chain nor a band split.
#include <cstdint>
#include <cstring>
#include <fstream>
#include <random>
#include <string>
#include <vector>

namespace DdsTestWriter {

enum class Format { Dxt1, Dxt5, Rgba32, Rgb24 };

inline std::size_t LevelBytes(Format format, std::uint32_t width, std::uint32_t height) {
    const std::size_t blocks = std::size_t{(width + 3) / 4} * ((height + 3) / 4);
    switch (format) {
        case Format::Dxt1: return blocks * 8;
        case Format::Dxt5: return blocks * 16;
        case Format::Rgba32: return std::size_t{width} * height * 4;
        case Format::Rgb24: return std::size_t{width} * height * 3;
    }
    return 0;
}

/** A DDS of `format` with `mipCount` levels (1 = base only) of seeded random content. */
inline std::vector<std::uint8_t> Make(Format format, std::uint32_t width, std::uint32_t height, std::uint32_t mipCount,
                                      std::uint32_t seed = 1) {
    std::vector<std::uint8_t> file(4 + 124, 0);
    std::memcpy(file.data(), "DDS ", 4);
    const auto put = [&file](std::size_t offset, std::uint32_t value) { std::memcpy(&file[4 + offset], &value, 4); };
    const bool compressed = format == Format::Dxt1 || format == Format::Dxt5;
    put(0, 124);                                                       // dwSize
    put(4, 0x1 | 0x2 | 0x4 | 0x1000 | (mipCount > 1 ? 0x20000u : 0u)); // dwFlags
    put(8, height);
    put(12, width);
    put(16, static_cast<std::uint32_t>(LevelBytes(format, width, height)));
    put(24, mipCount);
    put(72, 32); // ddspf.dwSize
    if (compressed) {
        put(76, 0x4); // DDPF_FOURCC
        std::memcpy(&file[4 + 80], format == Format::Dxt1 ? "DXT1" : "DXT5", 4);
    } else if (format == Format::Rgba32) {
        put(76, 0x41); // RGB | ALPHAPIXELS
        put(84, 32);
        put(88, 0x000000FF);
        put(92, 0x0000FF00);
        put(96, 0x00FF0000);
        put(100, 0xFF000000);
    } else {
        put(76, 0x40); // RGB
        put(84, 24);
        put(88, 0x000000FF);
        put(92, 0x0000FF00);
        put(96, 0x00FF0000);
    }
    put(104, 0x1000 | (mipCount > 1 ? 0x400008u : 0u)); // dwCaps

    std::mt19937 rng(seed);
    std::uint32_t w = width, h = height;
    for (std::uint32_t level = 0; level < mipCount; ++level) {
        const std::size_t bytes = LevelBytes(format, w, h);
        for (std::size_t i = 0; i < bytes; ++i) {
            file.push_back(static_cast<std::uint8_t>(rng()));
        }
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
    }
    return file;
}

inline void Write(const std::string& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

} // namespace DdsTestWriter

#endif // SOLARSYSTEM_TESTS_DDS_TEST_WRITER_H
