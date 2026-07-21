#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include <glm/glm.hpp>

#include "PlanetSystemManifest.h"
#include "Auxiliary_Modules/PlanetManifestLoader.h"

namespace fs = std::filesystem;

namespace {

class TempManifestFile {
public:
    explicit TempManifestFile(std::string contents)
        : _path(fs::temp_directory_path() / "solar_manifest_test.json") {
        std::ofstream out(_path);
        out << contents;
        _pathString = _path.string();
    }

    ~TempManifestFile() {
        std::error_code ec;
        fs::remove(_path, ec);
    }

    const std::string& path() const { return _pathString; }

private:
    fs::path _path;
    std::string _pathString;
};

const char* kValidManifest = R"({
  "version": 3,
  "systems": [
    {
      "name": "Mercury",
      "init": "Mercury",
      "proxyPosition": [1500, 0, 350],
      "activationRadius": 800,
      "required": ["resource/textures_low/Mercury_Diffuse_Low.dds"],
      "optional": ["resource/textures_low/Mercury_Normal_Low.dds"],
      "optionalHighRes": ["resource/textures/Mercury_Diffuse.dds"]
    }
  ]
})";

std::function<std::function<void()>(const std::string&)> makeInitFactory() {
    return [](const std::string& initTag) -> std::function<void()> {
        if (initTag == "Mercury") {
            return [] {};
        }
        return {};
    };
}

} // namespace

TEST(PlanetManifestLoaderTest, LoadsValidManifestWithVersion) {
    TempManifestFile manifest(kValidManifest);
    int version = -1;
    std::string error;

    const auto manifests = PlanetManifestLoader::LoadManifests(
        manifest.path(), glm::vec3(10.0f, 0.0f, 0.0f), makeInitFactory(), version, error);

    ASSERT_EQ(manifests.size(), 1u);
    EXPECT_EQ(version, 3);
    EXPECT_TRUE(error.empty());
    EXPECT_EQ(manifests[0].name, "Mercury");
    EXPECT_FLOAT_EQ(manifests[0].activationRadius, 800.0f);
    EXPECT_TRUE(glm::length(manifests[0].proxyPosition - glm::vec3(1510.0f, 0.0f, 350.0f)) < 0.001f);
    EXPECT_EQ(manifests[0].assetPaths.size(), 1u);
    EXPECT_EQ(manifests[0].optionalAssetPaths.size(), 1u);
    EXPECT_EQ(manifests[0].optionalHighResAssetPaths.size(), 1u);
    ASSERT_TRUE(static_cast<bool>(manifests[0].initFunc));
}

TEST(PlanetManifestLoaderTest, RejectsMalformedJson) {
    TempManifestFile manifest("{ not json");
    int version = 0;
    std::string error;

    const auto manifests = PlanetManifestLoader::LoadManifests(
        manifest.path(), glm::vec3(0.0f), makeInitFactory(), version, error);

    EXPECT_TRUE(manifests.empty());
    EXPECT_FALSE(error.empty());
}

TEST(PlanetManifestLoaderTest, RejectsMissingRequiredFields) {
    TempManifestFile manifest(R"({
      "version": 1,
      "systems": [
        {
          "name": "Mercury",
          "init": "Mercury",
          "proxyPosition": [1500, 0, 350],
          "activationRadius": 800,
          "required": []
        }
      ]
    })");
    int version = 0;
    std::string error;

    const auto manifests = PlanetManifestLoader::LoadManifests(
        manifest.path(), glm::vec3(0.0f), makeInitFactory(), version, error);

    EXPECT_TRUE(manifests.empty());
    EXPECT_NE(error.find("no required assets"), std::string::npos);
}

TEST(PlanetManifestLoaderTest, RejectsInvalidActivationRadius) {
    TempManifestFile manifest(R"({
      "version": 1,
      "systems": [
        {
          "name": "Mercury",
          "init": "Mercury",
          "proxyPosition": [1500, 0, 350],
          "activationRadius": 0,
          "required": ["resource/textures_low/Mercury_Diffuse_Low.dds"]
        }
      ]
    })");
    int version = 0;
    std::string error;

    const auto manifests = PlanetManifestLoader::LoadManifests(
        manifest.path(), glm::vec3(0.0f), makeInitFactory(), version, error);

    EXPECT_TRUE(manifests.empty());
    EXPECT_NE(error.find("invalid activationRadius"), std::string::npos);
}

TEST(PlanetManifestLoaderTest, RejectsUnknownInitTag) {
    TempManifestFile manifest(R"({
      "version": 1,
      "systems": [
        {
          "name": "Mercury",
          "init": "UnknownSystem",
          "proxyPosition": [1500, 0, 350],
          "activationRadius": 800,
          "required": ["resource/textures_low/Mercury_Diffuse_Low.dds"]
        }
      ]
    })");
    int version = 0;
    std::string error;

    const auto manifests = PlanetManifestLoader::LoadManifests(
        manifest.path(), glm::vec3(0.0f), makeInitFactory(), version, error);

    EXPECT_TRUE(manifests.empty());
    EXPECT_NE(error.find("Unknown init tag"), std::string::npos);
}
