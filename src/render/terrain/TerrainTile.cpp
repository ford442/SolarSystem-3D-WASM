#include "TerrainTile.h"

#include "../../3rdparty/ktx2_reader.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <sstream>

namespace Terrain {

namespace {

constexpr const char* kMetadataKey = "SSterrain";

void UpdateRange(HeightTile& tile) {
    const auto [lowest, highest] = std::minmax_element(tile.heights.begin(), tile.heights.end());
    tile.minHeight = *lowest;
    tile.maxHeight = *highest;
}

/**
 * Child sample `g` (apron included) along one axis from the parent's samples `at(p)`: an even
 * sample is the parent's own, an odd one the Catmull-Rom midpoint of the four around it,
 * clamped to the samples the parent has. `offset` is 0 or 32, the quadrant's first sample.
 */
template <typename At>
double HalfStep(int offset, int g, const At& at) {
    constexpr int kLow = -kTileApron, kHigh = kTileQuads + kTileApron;
    const int p = offset + (g >> 1); // floor(g / 2), apron row −1 included
    if ((g & 1) == 0) {
        return at(std::clamp(p, kLow, kHigh));
    }
    static const double kHalf[4] = {-0.0625, 0.5625, 0.5625, -0.0625}; // Catmull-Rom at f = 0.5
    double sum = 0.0;
    for (int a = 0; a < 4; ++a) {
        sum += kHalf[a] * at(std::clamp(p - 1 + a, kLow, kHigh));
    }
    return sum;
}

} // namespace

std::string TileMetadata(float minM, float maxM, uint8_t childMask) {
    std::ostringstream out;
    out.precision(9);
    out << "minM=" << minM << " maxM=" << maxM << " children=" << static_cast<int>(childMask);
    return out.str();
}

std::optional<HeightTile> ParseHeightTile(const uint8_t* data, std::size_t size, const TileKey& key, std::string* error) {
    const auto fail = [error](const std::string& why) -> std::optional<HeightTile> {
        if (error) {
            *error = why;
        }
        return std::nullopt;
    };
    if (!ktx2::HasIdentifier(data, size)) {
        return fail("not a KTX2 file"); // a 404 page, or Vite's index.html fallback
    }
    ktx2::File file;
    try {
        file = ktx2::Parse(data, size, "terrain tile");
    } catch (const std::exception& parseError) {
        return fail(parseError.what());
    }
    const std::size_t samples = static_cast<std::size_t>(kTileStride) * kTileStride;
    if (file.vkFormat != kVkFormatR16Unorm || file.pixelWidth != kTileStride || file.pixelHeight != kTileStride ||
        file.levels.empty() || file.levels[0].byteLength < samples * 2) {
        return fail("expected " + std::to_string(kTileStride) + "x" + std::to_string(kTileStride) + " R16_UNORM");
    }
    const std::string* metadata = file.FindValue(kMetadataKey);
    if (!metadata) {
        return fail(std::string("no ") + kMetadataKey + " entry");
    }
    double minM = 0.0, maxM = 0.0;
    long children = 0;
    int found = 0;
    std::istringstream fields(*metadata);
    std::string field;
    while (fields >> field) {
        const std::size_t equals = field.find('=');
        if (equals == std::string::npos) {
            continue;
        }
        const std::string name = field.substr(0, equals);
        const char* value = field.c_str() + equals + 1;
        if (name == "minM") {
            minM = std::strtod(value, nullptr);
            found |= 1;
        } else if (name == "maxM") {
            maxM = std::strtod(value, nullptr);
            found |= 2;
        } else if (name == "children") {
            children = std::strtol(value, nullptr, 10);
            found |= 4;
        }
    }
    if (found != 7 || !std::isfinite(minM) || !std::isfinite(maxM) || maxM < minM || children < 0 || children > 15) {
        return fail(std::string("bad ") + kMetadataKey + " \"" + *metadata + "\"");
    }

    HeightTile tile;
    tile.key = key;
    tile.childMask = static_cast<uint8_t>(children);
    tile.heights.resize(samples);
    const uint8_t* texels = data + file.levels[0].byteOffset;
    const double step = (maxM - minM) / 65535.0;
    for (std::size_t i = 0; i < samples; ++i) {
        const unsigned value = static_cast<unsigned>(texels[2 * i]) | (static_cast<unsigned>(texels[2 * i + 1]) << 8);
        tile.heights[i] = static_cast<float>(minM + step * value);
    }
    UpdateRange(tile);
    return tile;
}

HeightTile FlatTile(const TileKey& key, float heightM) {
    HeightTile tile;
    tile.key = key;
    tile.heights.assign(static_cast<std::size_t>(kTileStride) * kTileStride, heightM);
    tile.minHeight = tile.maxHeight = heightM;
    tile.synthesized = true;
    return tile;
}

HeightTile UpsampleChild(const HeightTile& parent, int quadrant) {
    HeightTile child;
    child.key = parent.key.Child(quadrant);
    child.synthesized = true;
    child.heights.resize(static_cast<std::size_t>(kTileStride) * kTileStride);
    const int offsetX = kTileQuads / 2 * (quadrant & 1);
    const int offsetY = kTileQuads / 2 * ((quadrant >> 1) & 1);
    // Separable: along u for every parent row, then along v. (Rows the quadrant does not reach are skipped.)
    std::vector<double> rows(static_cast<std::size_t>(kTileStride) * kTileStride);
    const auto row = [&rows](int r, int gx) -> double& {
        return rows[static_cast<std::size_t>((r + kTileApron) * kTileStride + gx + kTileApron)];
    };
    const int firstRow = std::max(-kTileApron, offsetY - 2), lastRow = std::min(kTileQuads + kTileApron, offsetY + 34);
    for (int r = firstRow; r <= lastRow; ++r) {
        for (int gx = -kTileApron; gx <= kTileQuads + kTileApron; ++gx) {
            row(r, gx) = HalfStep(offsetX, gx, [&](int p) { return static_cast<double>(parent.At(p, r)); });
        }
    }
    for (int gy = -kTileApron; gy <= kTileQuads + kTileApron; ++gy) {
        for (int gx = -kTileApron; gx <= kTileQuads + kTileApron; ++gx) {
            const std::size_t at = static_cast<std::size_t>((gy + kTileApron) * kTileStride + gx + kTileApron);
            child.heights[at] = static_cast<float>(HalfStep(offsetY, gy, [&](int r) { return row(r, gx); }));
        }
    }
    UpdateRange(child);
    return child;
}

double SurfaceHeight(const HeightTile& tile, double gx, double gy) {
    gx = std::clamp(gx, 0.0, static_cast<double>(kTileQuads));
    gy = std::clamp(gy, 0.0, static_cast<double>(kTileQuads));
    const int i = std::min(static_cast<int>(gx), kTileQuads - 1);
    const int j = std::min(static_cast<int>(gy), kTileQuads - 1);
    const double fx = gx - i, fy = gy - j;
    const double h00 = tile.At(i, j), h10 = tile.At(i + 1, j), h01 = tile.At(i, j + 1), h11 = tile.At(i + 1, j + 1);
    if (fx >= fy) {
        return h00 + fx * (h10 - h00) + fy * (h11 - h10); // triangle (i,j) (i+1,j) (i+1,j+1)
    }
    return h00 + fy * (h01 - h00) + fx * (h11 - h01); // triangle (i,j) (i+1,j+1) (i,j+1)
}

} // namespace Terrain
