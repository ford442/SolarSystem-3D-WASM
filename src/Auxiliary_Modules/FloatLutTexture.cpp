#include "FloatLutTexture.h"

#include "../3rdparty/ktx2_reader.h"
#include "../Solar_System/AtmosphereModel.h"
#include "WebResourceFetcher.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

namespace {

constexpr std::uint32_t kVkFormatRgba16f = 97;

std::optional<FloatLut> Reject(const std::string& path, const std::string& why) {
    std::cout << "[FloatLut] " << path << ": " << why << "; using the fallback path" << std::endl;
    return std::nullopt;
}

} // namespace

std::optional<FloatLut> LoadRgba16fLut(const std::string& path, int expectedWidth, int expectedHeight) {
    // ResourceExists, not RequireResident: a missing LUT is an expected, quiet fallback,
    // and RequireResident reports through std::cerr (console.error on web).
    if (!WebResourceFetcher::ResourceExists(path)) {
        return Reject(path, "not present");
    }
    std::ifstream stream(path, std::ios::binary);
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    if (bytes.empty()) {
        return Reject(path, "empty or unreadable");
    }

    ktx2::File file;
    try {
        file = ktx2::Parse(bytes.data(), bytes.size(), path);
    } catch (const std::exception& error) {
        return Reject(path, error.what());
    }
    if (file.vkFormat != kVkFormatRgba16f || file.levels.size() != 1 ||
        file.pixelWidth != static_cast<std::uint32_t>(expectedWidth) ||
        file.pixelHeight != static_cast<std::uint32_t>(expectedHeight)) {
        return Reject(path, "expected one " + std::to_string(expectedWidth) + "x" + std::to_string(expectedHeight) +
                                " RGBA16F level");
    }
    const ktx2::Level& level = file.levels[0];
    if (level.byteLength < static_cast<std::size_t>(expectedWidth) * expectedHeight * 8) {
        return Reject(path, "level is shorter than its declared size");
    }

    FloatLut lut;
    if (const std::string* hash = file.FindValue(AtmosphereModel::kKeyParamsHash)) {
        lut.paramsHash = *hash;
    }
    if (const std::string* version = file.FindValue(AtmosphereModel::kKeyMappingVersion)) {
        lut.mappingVersion = std::atoi(version->c_str());
    }

    while (glGetError() != GL_NO_ERROR) {}
    GLint previous = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);
    glGenTextures(1, &lut.texture);
    glBindTexture(GL_TEXTURE_2D, lut.texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, expectedWidth, expectedHeight, 0, GL_RGBA, GL_HALF_FLOAT,
                 bytes.data() + level.byteOffset);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    const GLenum error = glGetError();
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous));
    if (error != GL_NO_ERROR) {
        glDeleteTextures(1, &lut.texture);
        return Reject(path, "GL rejected the upload (error " + std::to_string(error) + ")");
    }
    return lut;
}
