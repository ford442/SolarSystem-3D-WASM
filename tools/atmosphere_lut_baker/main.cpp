// Offline baker for the physically based atmosphere LUTs.
//
//   atmosphere_lut_baker [--out resource/atmosphere] [--body earth] [--check]
//
// For every catalog atmosphere with a `physical` block (BodyCatalog::kAtmospheres, compiled
// in from planets.catalog.json) writes <out>/<id>_transmittance.ktx2 and
// <out>/<id>_multiscatter.ktx2 as RGBA16F KTX2 with the parameter hash in the key/value
// data. --check re-bakes in memory and fails if a committed file is missing, was baked
// from different parameters, or differs beyond half-float rounding — CI runs it as the
// AtmosphereLutsFresh test. See docs/ARCHITECTURE.md §9.2.

#include "3rdparty/ktx2_reader.h"
#include "3rdparty/ktx2_writer.h"
#include "Solar_System/AtmosphereModel.h"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

namespace {

std::optional<std::vector<std::uint8_t>> ReadFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

bool WriteFile(const std::string& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(file);
}

std::vector<std::pair<std::string, std::string>> KeyValues(const BodyCatalog::AtmospherePhysical& physical) {
    return {
        {"KTXwriter", "SolarSystem atmosphere_lut_baker"},
        {AtmosphereModel::kKeyParamsHash, AtmosphereModel::ParamsHashHex(physical)},
        {AtmosphereModel::kKeyMappingVersion, std::to_string(AtmosphereModel::kLutMappingVersion)},
    };
}

// The committed file must match a fresh bake to within half-float rounding. libm differs
// between platforms in the last bits, so compare decoded values, not bytes.
bool CheckFile(const std::string& path, const AtmosphereModel::Lut& fresh,
               const BodyCatalog::AtmospherePhysical& physical) {
    const auto bytes = ReadFile(path);
    if (!bytes) {
        std::cerr << "MISSING " << path << std::endl;
        return false;
    }
    ktx2::File file;
    try {
        file = ktx2::Parse(bytes->data(), bytes->size(), path);
    } catch (const std::exception& error) {
        std::cerr << "INVALID " << error.what() << std::endl;
        return false;
    }
    const std::string* hash = file.FindValue(AtmosphereModel::kKeyParamsHash);
    const std::string expectedHash = AtmosphereModel::ParamsHashHex(physical);
    if (!hash || *hash != expectedHash) {
        std::cerr << "STALE " << path << ": baked from parameters " << (hash ? *hash : "<none>")
                  << ", catalog is " << expectedHash << std::endl;
        return false;
    }
    if (file.vkFormat != ktx2::kVkFormatR16G16B16A16Sfloat || file.pixelWidth != static_cast<std::uint32_t>(fresh.width) ||
        file.pixelHeight != static_cast<std::uint32_t>(fresh.height) || file.levels.size() != 1 ||
        file.levels[0].byteLength != fresh.rgba.size() * 2) {
        std::cerr << "INVALID " << path << ": expected one " << fresh.width << "x" << fresh.height
                  << " RGBA16F level" << std::endl;
        return false;
    }
    const std::uint8_t* texels = bytes->data() + file.levels[0].byteOffset;
    for (std::size_t i = 0; i < fresh.rgba.size(); ++i) {
        const std::uint16_t half = static_cast<std::uint16_t>(texels[2 * i] | (texels[2 * i + 1] << 8));
        const float stored = ktx2::HalfToFloat(half);
        const float expected = ktx2::HalfToFloat(ktx2::FloatToHalf(fresh.rgba[i]));
        if (std::abs(stored - expected) > std::abs(expected) * (1.0f / 512.0f) + 1e-6f) {
            std::cerr << "DRIFT " << path << ": texel component " << i << " is " << stored << ", a fresh bake gives "
                      << expected << std::endl;
            return false;
        }
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    std::string outDir = AtmosphereModel::kLutDirectory;
    std::string onlyBody;
    bool check = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--out" && i + 1 < argc) {
            outDir = argv[++i];
        } else if (arg == "--body" && i + 1 < argc) {
            onlyBody = argv[++i];
        } else if (arg == "--check") {
            check = true;
        } else {
            std::cerr << "usage: atmosphere_lut_baker [--out DIR] [--body ID] [--check]" << std::endl;
            return 2;
        }
    }
    // Paths in AtmosphereModel are "resource/atmosphere/<file>"; keep only the file name.
    const auto inOutDir = [&](const std::string& resourcePath) {
        return outDir + "/" + resourcePath.substr(resourcePath.find_last_of('/') + 1);
    };

    int failures = 0;
    int baked = 0;
    for (const BodyCatalog::AtmosphereRow& row : BodyCatalog::kAtmospheres) {
        if (!row.physical.enabled || (!onlyBody.empty() && onlyBody != row.bodyId)) {
            continue;
        }
        const AtmosphereModel::Params params = AtmosphereModel::FromCatalog(row.physical);
        const AtmosphereModel::Lut transmittance = AtmosphereModel::BakeTransmittance(params);
        const AtmosphereModel::Lut multiScattering = AtmosphereModel::BakeMultiScattering(params, transmittance);
        const std::string transmittancePath = inOutDir(AtmosphereModel::TransmittanceLutPath(row.bodyId));
        const std::string multiScatteringPath = inOutDir(AtmosphereModel::MultiScatteringLutPath(row.bodyId));
        ++baked;

        if (check) {
            const bool ok = CheckFile(transmittancePath, transmittance, row.physical) &
                            CheckFile(multiScatteringPath, multiScattering, row.physical);
            std::cout << (ok ? "fresh " : "STALE ") << row.bodyId << std::endl;
            failures += ok ? 0 : 1;
            continue;
        }

        const auto keyValues = KeyValues(row.physical);
        if (!WriteFile(transmittancePath, ktx2::WriteRgba16f(transmittance.width, transmittance.height,
                                                             transmittance.rgba, keyValues)) ||
            !WriteFile(multiScatteringPath, ktx2::WriteRgba16f(multiScattering.width, multiScattering.height,
                                                               multiScattering.rgba, keyValues))) {
            std::cerr << "cannot write LUTs for " << row.bodyId << " into " << outDir << std::endl;
            ++failures;
            continue;
        }
        std::cout << "baked " << row.bodyId << " (" << AtmosphereModel::ParamsHashHex(row.physical) << ")" << std::endl;
    }

    if (baked == 0) {
        std::cerr << "no physical atmosphere" << (onlyBody.empty() ? "" : " for " + onlyBody) << " in the catalog"
                  << std::endl;
        return 1;
    }
    if (check && failures != 0) {
        std::cerr << "\nAtmosphere LUTs are out of date. Rebuild and run:\n"
                     "  atmosphere_lut_baker --out resource/atmosphere\n"
                     "(target atmosphere_lut_baker; configure with -DSOLARSYSTEM_BUILD_TOOLS=ON) and commit the results."
                  << std::endl;
    }
    return failures == 0 ? 0 : 1;
}
