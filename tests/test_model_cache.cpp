#include <gtest/gtest.h>

#include "assets/ModelCache.h"
#include "assets/TextureDecode.h"
#include "core/JobSystem.h"

#include <filesystem>
#include <fstream>
#include <glm/geometric.hpp>
#include <stdexcept>
#include <string>

namespace {

const std::string kSphere = std::string(SOLARSYSTEM_SOURCE_DIR) + "/resource/models/sphere.obj";

class ModelCacheTest : public ::testing::Test {
protected:
    void SetUp() override { ModelCache::Clear(); }
    void TearDown() override {
        ModelCache::Clear();
        Jobs::Stop();
    }
};

} // namespace

TEST_F(ModelCacheTest, ParsesTheCommittedSphere) {
    const MeshPayload payload = ParseModel(kSphere, ReadTextureFile(kSphere));
    ASSERT_FALSE(payload.parts.empty());
    ASSERT_TRUE(payload.triangles);
    EXPECT_GT(payload.boundingRadius, 0.5f);
    EXPECT_EQ(payload.triangles->indices.size() % 3, 0u);
    EXPECT_EQ(payload.triangles->positions.size(), payload.triangles->vertices.size());
    EXPECT_GT(payload.triangles->maxEdge, 0.0f);
    // Triangulate + tangent space: every textured vertex got a unit-ish normal.
    EXPECT_NEAR(glm::length(payload.parts[0].vertices[0].normal), 1.0f, 1e-3f);
}

TEST_F(ModelCacheTest, APrefetchedParseIsSharedByEveryGet) {
    Jobs::Start(2);
    ModelCache::Prefetch(kSphere);
    const auto first = ModelCache::Get(kSphere);
    const auto second = ModelCache::Get(kSphere);
    EXPECT_EQ(first.get(), second.get());
    const MeshPayload inline_ = ParseModel(kSphere, ReadTextureFile(kSphere));
    EXPECT_EQ(first->triangles->indices, inline_.triangles->indices);
    EXPECT_EQ(first->boundingRadius, inline_.boundingRadius);
}

TEST_F(ModelCacheTest, GetWithoutAPrefetchParsesInline) {
    EXPECT_NE(ModelCache::Get(kSphere), nullptr);
}

TEST_F(ModelCacheTest, MissingOrBrokenModelsThrowAndAreNotCached) {
    EXPECT_THROW(ModelCache::Get("resource/models/no_such.obj"), std::runtime_error);
    const std::string broken = (std::filesystem::temp_directory_path() / "broken_model.notamodel").string();
    {
        std::ofstream out(broken);
        out << "this is not a model\n";
    }
    EXPECT_THROW(ModelCache::Get(broken), std::runtime_error);
    EXPECT_THROW(ModelCache::Get(broken), std::runtime_error); // retried, not a cached failure
    std::filesystem::remove(broken);
}
