// Covers the GL-free atmosphere model behind the baked LUTs: the texture mappings (and that
// the GLSL mirror agrees), the transmittance integral against a closed form, the
// multi-scattering table's range, thicknessScale, the parameter hash, and the KTX2
// RGBA16F writer/reader round trip the baker relies on.

#include "3rdparty/ktx2_reader.h"
#include "3rdparty/ktx2_writer.h"
#include "Solar_System/AtmosphereModel.h"

#include <gtest/gtest.h>

#include <cmath>
#include <fstream>
#include <iterator>
#include <regex>
#include <tuple>
#include <sstream>

namespace {

using namespace AtmosphereModel;

const BodyCatalog::AtmospherePhysical& Earth() {
    const BodyCatalog::AtmosphereRow* row = BodyCatalog::FindAtmosphere("earth");
    EXPECT_NE(row, nullptr);
    EXPECT_TRUE(row->physical.enabled);
    return row->physical;
}

TEST(AtmosphereModel, TransmittanceMappingRoundTrips) {
    const Params params = FromCatalog(Earth());
    for (double xr : {0.0, 0.1, 0.37, 0.8, 1.0}) {
        for (double xmu : {0.0, 0.25, 0.5, 0.9, 1.0}) {
            const glm::dvec2 uv(TexCoordFromUnitRange(xmu, kTransmittanceWidth),
                                TexCoordFromUnitRange(xr, kTransmittanceHeight));
            double r, mu;
            RMuFromTransmittanceUv(params, uv, r, mu);
            EXPECT_GE(r, params.bottomRadius - 1e-9);
            EXPECT_LE(r, params.topRadius + 1e-9);
            EXPECT_FALSE(RayIntersectsGround(params, r, mu - 1e-9) && xmu < 0.999)
                << "the table only covers rays that miss the ground";
            const glm::dvec2 back = TransmittanceUvFromRMu(params, r, mu);
            EXPECT_NEAR(back.x, uv.x, 1e-9) << "xr=" << xr << " xmu=" << xmu;
            EXPECT_NEAR(back.y, uv.y, 1e-9) << "xr=" << xr << " xmu=" << xmu;
        }
    }
}

TEST(AtmosphereModel, MultiScatteringMappingRoundTrips) {
    const Params params = FromCatalog(Earth());
    for (double r : {params.bottomRadius, params.bottomRadius + 37.0, params.topRadius}) {
        for (double muS : {-1.0, -0.2, 0.0, 0.6, 1.0}) {
            const glm::dvec2 uv = MultiScatteringUvFromRMuS(params, r, muS);
            double r2, muS2;
            RMuSFromMultiScatteringUv(params, uv, r2, muS2);
            EXPECT_NEAR(r2, r, 1e-9);
            EXPECT_NEAR(muS2, muS, 1e-9);
        }
    }
}

TEST(AtmosphereModel, EarthZenithTransmittanceMatchesClosedForm) {
    const Params params = FromCatalog(Earth());
    const double depth = params.topRadius - params.bottomRadius;
    // Straight up from the ground the path is radial, so each exponential layer integrates
    // to sigma * H * (1 - exp(-depth / H)) and the ozone tent (fully inside) to sigma * w.
    const glm::dvec3 tau =
        params.rayleighScattering * params.rayleighScaleHeight * (1.0 - std::exp(-depth / params.rayleighScaleHeight)) +
        params.mieExtinction * params.mieScaleHeight * (1.0 - std::exp(-depth / params.mieScaleHeight)) +
        params.absorptionExtinction * params.absorptionHalfWidth;
    const glm::dvec3 expected(std::exp(-tau.x), std::exp(-tau.y), std::exp(-tau.z));
    const glm::dvec3 integrated = IntegrateTransmittance(params, params.bottomRadius, 1.0);
    for (int c = 0; c < 3; ++c) {
        EXPECT_NEAR(integrated[c], expected[c], 1e-3) << "channel " << c;
    }
    // Earth's sky: blue is scattered out most.
    EXPECT_GT(integrated.x, integrated.z);
}

TEST(AtmosphereModel, TransmittanceFallsTowardTheHorizon) {
    const Params params = FromCatalog(Earth());
    const double r = params.bottomRadius + 1.0;
    glm::dvec3 previous = IntegrateTransmittance(params, r, 1.0);
    for (double mu = 0.9; mu >= 0.0; mu -= 0.1) {
        const glm::dvec3 t = IntegrateTransmittance(params, r, mu);
        EXPECT_LE(t.z, previous.z + 1e-12) << "mu=" << mu;
        previous = t;
    }
}

TEST(AtmosphereModel, BakedTablesStayInRange) {
    for (const BodyCatalog::AtmosphereRow& row : BodyCatalog::kAtmospheres) {
        if (!row.physical.enabled) {
            continue;
        }
        const Params params = FromCatalog(row.physical);
        const Lut transmittance = BakeTransmittance(params);
        ASSERT_EQ(transmittance.width, kTransmittanceWidth);
        ASSERT_EQ(transmittance.height, kTransmittanceHeight);
        for (float v : transmittance.rgba) {
            EXPECT_GE(v, 0.0f) << row.bodyId;
            EXPECT_LE(v, 1.0f) << row.bodyId;
        }
        // The LUT reproduces the direct integral at a texel centre.
        const glm::dvec3 texel = transmittance.Texel(100, 40);
        double r, mu;
        RMuFromTransmittanceUv(params, {(100 + 0.5) / kTransmittanceWidth, (40 + 0.5) / kTransmittanceHeight}, r, mu);
        const glm::dvec3 direct = IntegrateTransmittance(params, r, mu);
        EXPECT_NEAR(texel.y, direct.y, 1e-6) << row.bodyId;

        const Lut multi = BakeMultiScattering(params, transmittance);
        ASSERT_EQ(multi.width, kMultiScatteringSize);
        float maxPsi = 0.0f;
        for (std::size_t i = 0; i < multi.rgba.size(); i += 4) {
            for (int c = 0; c < 3; ++c) {
                EXPECT_GE(multi.rgba[i + c], 0.0f) << row.bodyId;
                EXPECT_TRUE(std::isfinite(multi.rgba[i + c])) << row.bodyId;
                maxPsi = std::max(maxPsi, multi.rgba[i + c]);
            }
        }
        EXPECT_GT(maxPsi, 0.0f) << row.bodyId << ": no multiple scattering at all";
        EXPECT_LT(maxPsi, 1.0f) << row.bodyId << ": Psi_ms is per unit illuminance; >= 1 means a bug";
        // Half floats: values must not underflow to zero where the sun is up.
        const glm::dvec3 noon = multi.Sample(MultiScatteringUvFromRMuS(params, params.bottomRadius, 1.0));
        EXPECT_GT(noon.y, 6.1e-5) << row.bodyId << ": below the RGBA16F normal range";
    }
}

TEST(AtmosphereModel, NightSideReceivesNoSingleScatteringLight) {
    const Params params = FromCatalog(Earth());
    const Lut transmittance = BakeTransmittance(params);
    EXPECT_EQ(LookupTransmittance(params, transmittance, params.bottomRadius + 1.0, -0.5), glm::dvec3(0.0));
}

TEST(AtmosphereModel, ThicknessScalePreservesVerticalOpticalDepth) {
    BodyCatalog::AtmospherePhysical stretched = Earth();
    stretched.thicknessScale = 3.0f;
    const Params base = FromCatalog(Earth());
    const Params wide = FromCatalog(stretched);
    EXPECT_NEAR(wide.topRadius - wide.bottomRadius, 3.0 * (base.topRadius - base.bottomRadius), 1e-9);
    const glm::dvec3 a = IntegrateTransmittance(base, base.bottomRadius, 1.0, 4000);
    const glm::dvec3 b = IntegrateTransmittance(wide, wide.bottomRadius, 1.0, 4000);
    for (int c = 0; c < 3; ++c) {
        EXPECT_NEAR(a[c], b[c], 1e-4) << "channel " << c;
    }
}

TEST(AtmosphereModel, ParamsHashTracksLutInputsOnly) {
    const BodyCatalog::AtmospherePhysical& earth = Earth();
    EXPECT_EQ(ParamsHash(earth), ParamsHash(earth));
    EXPECT_EQ(ParamsHashHex(earth).size(), 16u);

    BodyCatalog::AtmospherePhysical changed = earth;
    changed.mieScaleHeightKm += 0.1f;
    EXPECT_NE(ParamsHash(changed), ParamsHash(earth));

    BodyCatalog::AtmospherePhysical brighter = earth;
    brighter.exposure *= 2.0f;
    EXPECT_EQ(ParamsHash(brighter), ParamsHash(earth)) << "exposure is applied at runtime, not baked";
}

TEST(AtmosphereModel, LutPathsFollowTheBodyId) {
    EXPECT_EQ(TransmittanceLutPath("titan"), "resource/atmosphere/titan_transmittance.ktx2");
    EXPECT_EQ(MultiScatteringLutPath("titan"), "resource/atmosphere/titan_multiscatter.ktx2");
}

#ifdef SOLARSYSTEM_SOURCE_DIR
// atmosphere_lut.glsl mirrors the C++ mapping. Same sizes and version, or the shader reads
// the tables with the wrong parameterisation.
TEST(AtmosphereModel, GlslMirrorAgreesWithCpp) {
    std::ifstream file(std::string(SOLARSYSTEM_SOURCE_DIR) + "/resource/shaders/common/atmosphere_lut.glsl");
    ASSERT_TRUE(file.good());
    std::stringstream buffer;
    buffer << file.rdbuf();
    const std::string glsl = buffer.str();

    const auto number = [&](const std::string& pattern) {
        std::smatch match;
        EXPECT_TRUE(std::regex_search(glsl, match, std::regex(pattern))) << pattern;
        return match.size() > 1 ? std::stod(match[1].str()) : -1.0;
    };
    EXPECT_EQ(number(R"(#define ATMO_LUT_MAPPING_VERSION (\d+))"), kLutMappingVersion);
    EXPECT_EQ(number(R"(TRANSMITTANCE_LUT_WIDTH = ([\d.]+);)"), kTransmittanceWidth);
    EXPECT_EQ(number(R"(TRANSMITTANCE_LUT_HEIGHT = ([\d.]+);)"), kTransmittanceHeight);
    EXPECT_EQ(number(R"(MULTISCATTERING_LUT_SIZE = ([\d.]+);)"), kMultiScatteringSize);
}
#endif

// ---------------------------------------------------------------------------------------
// KTX2 RGBA16F

TEST(Ktx2Writer, HalfFloatConversionRoundsToNearest) {
    EXPECT_EQ(ktx2::FloatToHalf(0.0f), 0x0000);
    EXPECT_EQ(ktx2::FloatToHalf(1.0f), 0x3C00);
    EXPECT_EQ(ktx2::FloatToHalf(-2.0f), 0xC000);
    EXPECT_EQ(ktx2::FloatToHalf(65504.0f), 0x7BFF);
    EXPECT_EQ(ktx2::FloatToHalf(1e6f), 0x7C00);       // overflow -> +inf
    EXPECT_EQ(ktx2::FloatToHalf(5.96046448e-8f), 0x0001); // smallest subnormal
    for (float v : {0.0f, 1.0f, 0.333f, 1e-3f, 3e-5f, 0.99951171875f, 1234.5f}) {
        const float back = ktx2::HalfToFloat(ktx2::FloatToHalf(v));
        EXPECT_NEAR(back, v, std::max(std::abs(v) / 1024.0f, 6e-8f)) << v;
    }
}

TEST(Ktx2Writer, Rgba16fRoundTripsThroughTheReader) {
    const std::vector<float> rgba = {0.0f, 0.25f, 0.5f, 1.0f, 1e-3f, 2.0f, 4.0f, 1.0f,
                                     0.1f, 0.2f,  0.3f, 1.0f, 0.75f, 0.5f, 0.25f, 1.0f};
    const std::vector<std::uint8_t> bytes =
        ktx2::WriteRgba16f(2, 2, rgba, {{"b.key", "two"}, {"a.key", "one"}, {"KTXwriter", "test"}});
    const ktx2::File file = ktx2::Parse(bytes.data(), bytes.size(), "roundtrip");
    EXPECT_EQ(file.vkFormat, ktx2::kVkFormatR16G16B16A16Sfloat);
    EXPECT_EQ(file.typeSize, 2u);
    EXPECT_EQ(file.pixelWidth, 2u);
    EXPECT_EQ(file.pixelHeight, 2u);
    ASSERT_EQ(file.levels.size(), 1u);
    EXPECT_EQ(file.levels[0].byteLength, rgba.size() * 2);
    EXPECT_EQ(file.levels[0].byteOffset % 8, 0u);

    ASSERT_EQ(file.keyValues.size(), 3u);
    EXPECT_EQ(file.keyValues[0].first, "KTXwriter"); // sorted by key
    ASSERT_NE(file.FindValue("a.key"), nullptr);
    EXPECT_EQ(*file.FindValue("a.key"), "one");
    EXPECT_EQ(*file.FindValue("b.key"), "two");
    EXPECT_EQ(file.FindValue("missing"), nullptr);

    for (std::size_t i = 0; i < rgba.size(); ++i) {
        const std::uint8_t* p = bytes.data() + file.levels[0].byteOffset + 2 * i;
        const float value = ktx2::HalfToFloat(static_cast<std::uint16_t>(p[0] | (p[1] << 8)));
        EXPECT_NEAR(value, rgba[i], std::abs(rgba[i]) / 1024.0f + 1e-7f) << i;
    }
}

TEST(Ktx2Writer, RejectsMismatchedPixelCount) {
    EXPECT_THROW(ktx2::WriteRgba16f(2, 2, std::vector<float>(15, 0.0f), {}), std::invalid_argument);
}

#ifdef SOLARSYSTEM_SOURCE_DIR
// The committed LUTs parse, are the right shape, and were baked from the current catalog.
// (The AtmosphereLutsFresh ctest re-bakes and compares values; this is the cheap check.)
TEST(AtmosphereModel, CommittedLutsMatchTheCatalog) {
    for (const BodyCatalog::AtmosphereRow& row : BodyCatalog::kAtmospheres) {
        if (!row.physical.enabled) {
            continue;
        }
        for (const auto& [path, width, height] :
             {std::tuple{TransmittanceLutPath(row.bodyId), kTransmittanceWidth, kTransmittanceHeight},
              std::tuple{MultiScatteringLutPath(row.bodyId), kMultiScatteringSize, kMultiScatteringSize}}) {
            std::ifstream file(std::string(SOLARSYSTEM_SOURCE_DIR) + "/" + path, std::ios::binary);
            ASSERT_TRUE(file.good()) << path << " is missing; run atmosphere_lut_baker";
            const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            const ktx2::File parsed = ktx2::Parse(bytes.data(), bytes.size(), path);
            EXPECT_EQ(parsed.vkFormat, ktx2::kVkFormatR16G16B16A16Sfloat) << path;
            EXPECT_EQ(parsed.pixelWidth, static_cast<std::uint32_t>(width)) << path;
            EXPECT_EQ(parsed.pixelHeight, static_cast<std::uint32_t>(height)) << path;
            const std::string* hash = parsed.FindValue(kKeyParamsHash);
            ASSERT_NE(hash, nullptr) << path;
            EXPECT_EQ(*hash, ParamsHashHex(row.physical)) << path << " is stale; run atmosphere_lut_baker";
        }
    }
}
#endif

} // namespace
