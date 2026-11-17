#include <gtest/gtest.h>

#include <string>

#include "core/Json.h"

TEST(JsonTest, ParsesNestedDocument) {
    Json::Value v;
    std::string error;
    ASSERT_TRUE(Json::Parse(R"({"a": [1, 2.5, -3e2], "b": {"c": "d", "e": true, "f": null}})", v, error)) << error;
    EXPECT_TRUE(v.IsObject());
    EXPECT_EQ(v["a"].Size(), 3u);
    EXPECT_DOUBLE_EQ(v["a"][std::size_t{1}].AsNumber(), 2.5);
    EXPECT_DOUBLE_EQ(v["a"][std::size_t{2}].AsNumber(), -300.0);
    EXPECT_EQ(v["b"]["c"].AsString(), "d");
    EXPECT_TRUE(v["b"]["e"].AsBool());
    EXPECT_TRUE(v["b"]["f"].IsNull());
    EXPECT_TRUE(v["b"].Has("f"));
}

TEST(JsonTest, MissingKeysReadAsDefaults) {
    Json::Value v;
    std::string error;
    ASSERT_TRUE(Json::Parse(R"({"x": 1})", v, error));
    EXPECT_TRUE(v["nope"]["deeper"][std::size_t{3}].IsNull());
    EXPECT_DOUBLE_EQ(v["nope"].AsNumber(7.0), 7.0);
    EXPECT_EQ(v["x"].AsString("fallback"), "fallback");
}

TEST(JsonTest, DecodesUnicodeEscapes) {
    Json::Value v;
    std::string error;
    ASSERT_TRUE(Json::Parse(R"(["Солнце", "🌍", "a\"b\\c"])", v, error)) << error;
    EXPECT_EQ(v[std::size_t{0}].AsString(), "Солнце");
    EXPECT_EQ(v[std::size_t{1}].AsString(), "\xF0\x9F\x8C\x8D");
    EXPECT_EQ(v[std::size_t{2}].AsString(), "a\"b\\c");
}

TEST(JsonTest, RejectsMalformedInput) {
    Json::Value v;
    std::string error;
    EXPECT_FALSE(Json::Parse(R"({"a": 1,})", v, error));
    EXPECT_FALSE(Json::Parse(R"({"a" 1})", v, error));
    EXPECT_FALSE(Json::Parse(R"([1, 2)", v, error));
    EXPECT_FALSE(Json::Parse(R"("unterminated)", v, error));
    EXPECT_FALSE(Json::Parse(R"({} trailing)", v, error));
    EXPECT_FALSE(error.empty());
}
