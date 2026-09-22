#include "TextureImage2D.h"
#include "WebResourceFetcher.h"
#include "GlCapabilities.h"
#include "BlockCompression.h"
#include "TextureFormatSupport.h"
#include "../3rdparty/ktx2_reader.h"
#include "../SimState.h"
#include <cstdint>
#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace {
    /** What a container upload left on the GPU, so the caller knows how to finish the texture. */
    struct UploadResult {
        bool hasMipmaps = false;         // a real chain is resident; a mip min-filter is safe
        bool allowGenerateMipmap = false; // uncompressed, so glGenerateMipmap can fill the chain
    };

    bool HasSuffix(const std::string& value, const std::string& suffix) {
        return value.size() > suffix.size() &&
               value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    bool IsGpuTextureValid(GLuint textureId, unsigned int width, unsigned int height) {
        if (textureId == 0) {
            return false;
        }

#ifdef __EMSCRIPTEN__
        // WebGL 2 (ES 3.0) does not provide glGetTexLevelParameteriv.
        // Dimensions were validated before GPU upload; non-zero size means upload succeeded.
        return width > 0 && height > 0;
#else
        GLint gpuWidth = 0;
        glBindTexture(GL_TEXTURE_2D, textureId);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &gpuWidth);
        return gpuWidth > 0;
#endif
    }

    std::vector<std::uint8_t> ReadWholeFile(const std::string& path) {
        std::ifstream stream(path, std::ios::binary | std::ios::ate);
        if (!stream) {
            throw std::runtime_error("cannot open " + path);
        }
        const std::streamoff size = stream.tellg();
        if (size <= 0) {
            throw std::runtime_error("empty file " + path);
        }
        stream.seekg(0, std::ios::beg);
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
        if (!stream.read(reinterpret_cast<char*>(bytes.data()), size)) {
            throw std::runtime_error("short read on " + path);
        }
        return bytes;
    }

    void ValidateAgainstMaxTextureSize(const std::string& path, unsigned int width, unsigned int height) {
        const int maxTextureSize = GetGlCapabilities().maxTextureSize;
        if (maxTextureSize <= 0) {
            return;
        }
        if (width > static_cast<unsigned int>(maxTextureSize) ||
            height > static_cast<unsigned int>(maxTextureSize)) {
            throw std::runtime_error(
                "Texture dimensions " + std::to_string(width) + "x" + std::to_string(height) +
                " exceed GL_MAX_TEXTURE_SIZE (" + std::to_string(maxTextureSize) + ") for " + path);
        }
    }

    /**
     * Upload a KTX2 file whose levels are already in a GPU block format (or plain RGBA8).
     * Nothing is transcoded here: the pack was encoded offline into the format this GPU
     * reported, so each level's bytes go straight to glCompressedTexImage2D. A pack whose
     * format this context cannot accept is an error, not a silent downgrade — the caller
     * falls back, and the console names the mismatch.
     */
    UploadResult UploadKtx2(const std::string& path, unsigned int& width, unsigned int& height) {
        const std::vector<std::uint8_t> bytes = ReadWholeFile(path);
        const ktx2::File file = ktx2::Parse(bytes.data(), bytes.size(), path);

        const std::uint32_t internalFormat = TextureFormats::GlInternalFormatFromVkFormat(file.vkFormat);
        if (internalFormat == 0) {
            throw std::runtime_error("KTX2 " + path + ": vkFormat " + std::to_string(file.vkFormat) +
                                     " has no GL mapping in this build");
        }
        if (!TextureFormats::IsFormatSupported(file.vkFormat, GetGlCapabilities().ToFormatCapabilities())) {
            throw std::runtime_error("KTX2 " + path + ": vkFormat " + std::to_string(file.vkFormat) +
                                     " is not supported by this GPU/browser (expected the '" +
                                     GetGlCapabilities().PreferredTexturePack() + "' pack)");
        }

        width = file.pixelWidth;
        height = std::max<std::uint32_t>(file.pixelHeight, 1);
        ValidateAgainstMaxTextureSize(path, width, height);

        const TextureFormats::BlockLayout layout = TextureFormats::LayoutFromVkFormat(file.vkFormat);
        const TextureFormats::UncompressedUpload uncompressed =
            TextureFormats::UncompressedUploadFromVkFormat(file.vkFormat);

        while (glGetError() != GL_NO_ERROR) {}

        int uploadedLevels = 0;
        for (std::size_t level = 0; level < file.levels.size(); ++level) {
            const ktx2::Level& entry = file.levels[level];
            const std::size_t expected =
                TextureFormats::ExpectedLevelBytes(file.vkFormat, entry.width, entry.height);
            if (expected == 0 || entry.byteLength < expected) {
                throw std::runtime_error("KTX2 " + path + ": level " + std::to_string(level) +
                                         " is shorter than its declared " + std::to_string(entry.width) +
                                         "x" + std::to_string(entry.height) + " payload");
            }

            const std::uint8_t* levelData = bytes.data() + entry.byteOffset;
            if (layout.compressed) {
                glCompressedTexImage2D(GL_TEXTURE_2D, static_cast<GLint>(level),
                                       static_cast<GLenum>(internalFormat),
                                       static_cast<GLsizei>(entry.width), static_cast<GLsizei>(entry.height),
                                       0, static_cast<GLsizei>(expected), levelData);
            } else {
                glTexImage2D(GL_TEXTURE_2D, static_cast<GLint>(level),
                             static_cast<GLint>(uncompressed.internalFormat),
                             static_cast<GLsizei>(entry.width), static_cast<GLsizei>(entry.height), 0,
                             static_cast<GLenum>(uncompressed.format),
                             static_cast<GLenum>(uncompressed.type), levelData);
            }

            const GLenum error = glGetError();
            if (error != GL_NO_ERROR) {
                if (level == 0) {
                    throw std::runtime_error("KTX2 " + path + ": GL rejected the base level (error 0x" +
                                             std::to_string(static_cast<unsigned>(error)) + ")");
                }
                // A rejected mip is survivable: cap the chain at what actually landed.
                break;
            }
            ++uploadedLevels;
        }

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, std::max(uploadedLevels - 1, 0));

        UploadResult result;
        result.hasMipmaps = uploadedLevels > 1;
        result.allowGenerateMipmap = !layout.compressed && uploadedLevels == 1;
        return result;
    }

    /**
     * CPU-decode a DXT DDS to RGBA8 and upload that instead. This is the path a GPU with
     * no WEBGL_compressed_texture_s3tc takes for the DDS content still in the tree; see
     * BlockCompression.h for why it is a stopgap rather than the shipping answer.
     */
    UploadResult UploadDdsSoftwareDecoded(CDDSImage& image, const std::string& path) {
        const std::uint32_t format = image.get_format();
        if (!BlockCompression::IsDecodableS3tcFormat(format)) {
            throw std::runtime_error(
                "Compressed textures are not supported by this GPU/browser and format 0x" +
                std::to_string(static_cast<unsigned>(format)) + " has no software decoder for " + path);
        }

        while (glGetError() != GL_NO_ERROR) {}

        // Level 0 is the CDDSImage itself; get_mipmap(i) is level i + 1.
        int uploadedLevels = 0;
        const unsigned int levelCount = image.get_num_mipmaps() + 1;
        for (unsigned int level = 0; level < levelCount; ++level) {
            const std::uint8_t* blocks = nullptr;
            unsigned int levelWidth = 0, levelHeight = 0, levelBytes = 0;
            if (level == 0) {
                blocks = static_cast<std::uint8_t*>(image);
                levelWidth = image.get_width();
                levelHeight = image.get_height();
                levelBytes = image.get_size();
            } else {
                const CSurface& surface = image.get_mipmap(level - 1);
                blocks = static_cast<std::uint8_t*>(surface);
                levelWidth = surface.get_width();
                levelHeight = surface.get_height();
                levelBytes = surface.get_size();
            }

            const std::vector<std::uint8_t> rgba =
                BlockCompression::DecodeS3tcToRgba8(format, blocks, levelBytes, levelWidth, levelHeight);
            if (rgba.empty()) {
                if (level == 0) {
                    throw std::runtime_error("Software DXT decode failed for " + path);
                }
                break;
            }

            glTexImage2D(GL_TEXTURE_2D, static_cast<GLint>(level), GL_RGBA8,
                         static_cast<GLsizei>(levelWidth), static_cast<GLsizei>(levelHeight), 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
            if (glGetError() != GL_NO_ERROR) {
                if (level == 0) {
                    throw std::runtime_error("GL rejected the software-decoded base level for " + path);
                }
                break;
            }
            ++uploadedLevels;
        }

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, std::max(uploadedLevels - 1, 0));

        UploadResult result;
        result.hasMipmaps = uploadedLevels > 1;
        result.allowGenerateMipmap = uploadedLevels == 1;
        return result;
    }
}

TextureImage2D::TextureImage2D(const std::string& path, GLint wrapParam, GLint minFilter, GLint magFilter) {
    LoadTextureFromFile(path, wrapParam, minFilter, magFilter, true);
}

void TextureImage2D::LoadTextureFromFile(const std::string& path, GLint wrapParam, GLint minFilter, GLint magFilter, bool allowFallback) {
    // On web the low tier is preloaded into the .data file and mid/high tiers arrive
    // through TextureLoadingQueue's async download, so the file is resident by now;
    // a miss falls through to CreateFallbackTexture() below.
    WebResourceFetcher::RequireResident(path, "TextureImage2D");

    glGenTextures(1, &_textureID);
    glBindTexture(GL_TEXTURE_2D, _textureID);

    bool canGenerateMipmaps = false;
    bool hasEmbeddedMipmaps = false;

    try {
        // [PORTING NOTE]
        // Ensure files are preloaded (emcc --preload-file) or fetched asynchronously.
        // For preloaded files, std::ifstream works transparently over MEMFS.
        if (HasSuffix(path, ".ktx2")) {
            // Format-specific pack (BC/ASTC/ETC2), chosen at startup from the probed caps.
            const UploadResult upload = UploadKtx2(path, _width, _height);
            hasEmbeddedMipmaps = upload.hasMipmaps;
            canGenerateMipmaps = upload.allowGenerateMipmap;
        } else {
            CDDSImage image;
            image.load(path, false);
            const bool isCompressed = image.is_compressed();
            _width = image.get_width();
            _height = image.get_height();
            ValidateAgainstMaxTextureSize(path, _width, _height);

            if (isCompressed && !GetGlCapabilities().s3tcCompressedTextures) {
                // Safari/iOS and most Android GPUs land here. Decode on the CPU and upload
                // RGBA8 rather than dropping the body to the fallback checkerboard.
                const UploadResult upload = UploadDdsSoftwareDecoded(image, path);
                hasEmbeddedMipmaps = upload.hasMipmaps;
                canGenerateMipmaps = upload.allowGenerateMipmap;
            } else {
                image.upload_texture2D();
                // hasEmbedded reflects what was *actually* uploaded (MAX_LEVEL>0).
                // For compressed DXT with short chains or partial failures, upload_texture2D
                // now sets MAX_LEVEL to the true last resident level.
                hasEmbeddedMipmaps = (image.get_num_mipmaps() > 0);
                canGenerateMipmaps = !isCompressed;
#ifdef __EMSCRIPTEN__
                GLint actualMax = 0;
                glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, &actualMax);
                hasEmbeddedMipmaps = (actualMax > 0);
#endif
            }
        }
    }
    catch (const std::runtime_error& error) {
#ifdef __EMSCRIPTEN__
        if (_textureID != 0) {
            glDeleteTextures(1, &_textureID);
            _textureID = 0;
        }
        if (!allowFallback) {
            throw;
        }
        // On web, many moon/ring/high-res textures are intentionally omitted from the
        // repository and deployment. Use a visible fallback so planets/moons never
        // appear solid black and the scene remains stable.
        std::cerr << "[Texture] Warning: Failed to load " << path << " (" << error.what()
                  << "). Creating fallback texture." << std::endl;
        CreateFallbackTexture(wrapParam, minFilter, magFilter);
        return;
#else
        throw std::runtime_error("Image " + path + " cannot be loaded");
#endif
    }

    // Compressed textures (S3TC/ETC2/ASTC/BPTC) do not support glGenerateMipmap in
    // WebGL 2 — their mip chain must come pre-embedded in the container. Only attempt
    // generation for uncompressed uploads that arrived without one.
    if (canGenerateMipmaps && !hasEmbeddedMipmaps) {
        // Clear GL error queue to prevent old errors from triggering false positives here
        while (glGetError() != GL_NO_ERROR);

        glGenerateMipmap(GL_TEXTURE_2D);
        GLenum err = glGetError();
        if (err != GL_NO_ERROR) {
            std::cerr << "Warning: glGenerateMipmap failed for " << path
                      << " (error 0x" << std::hex << err << std::dec << ")" << std::endl;
        } else {
            hasEmbeddedMipmaps = true;
#ifdef __EMSCRIPTEN__
            // After glGenerateMipmap the full mip chain is now resident on the GPU.
            // upload_texture2D() set MAX_LEVEL = 0 (no embedded mips in DDS), so we
            // must re-declare the range to include the newly generated levels.
            // Without this, WebGL 2 sees MAX_LEVEL=0 with a mip-filter and returns
            // only black (texture incomplete).
            {
                int maxDim = static_cast<int>(std::max(_width, _height));
                int numMipLevels = 0;
                while (maxDim > 1) { maxDim >>= 1; ++numMipLevels; }
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, numMipLevels);
            }
#endif
        }
    }

    // If no mipmaps are available (neither generated nor embedded), using any
    // mipmap min-filter makes the texture "incomplete" — degrade to GL_LINEAR.
    if (!hasEmbeddedMipmaps) {
        bool minFilterUsesMipmaps =
            (minFilter == GL_NEAREST_MIPMAP_NEAREST ||
             minFilter == GL_LINEAR_MIPMAP_NEAREST  ||
             minFilter == GL_NEAREST_MIPMAP_LINEAR  ||
             minFilter == GL_LINEAR_MIPMAP_LINEAR);
        if (minFilterUsesMipmaps) {
            // Mute this expected fallback warning for web builds to clean up the console
#ifndef __EMSCRIPTEN__
            std::cerr << "Warning: No mipmaps for " << path
                      << " -- falling back min filter to GL_LINEAR" << std::endl;
#endif
            minFilter = GL_LINEAR;
        }
    }

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapParam);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapParam);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilter);
#ifdef __EMSCRIPTEN__
    const auto& glCaps = GetGlCapabilities();
    if (glCaps.anisotropicFiltering) {
        const float cap = gSimState->isMobileWeb ? 4.0f : 8.0f;
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, std::min(glCaps.maxAnisotropy, cap));
    }
#else
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 16);
#endif

#ifdef __EMSCRIPTEN__
    // Runtime sanity check: if a mip filter is active and MAX_LEVEL is still 0,
    // the texture will render black in WebGL 2 (incomplete mip chain).
    {
        bool minFilterUsesMipmaps =
            (minFilter == GL_NEAREST_MIPMAP_NEAREST ||
             minFilter == GL_LINEAR_MIPMAP_NEAREST  ||
             minFilter == GL_NEAREST_MIPMAP_LINEAR  ||
             minFilter == GL_LINEAR_MIPMAP_LINEAR);
        if (minFilterUsesMipmaps) {
            GLint maxLevel = 0;
            glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, &maxLevel);
            if (maxLevel == 0) {
                std::cerr << "[Texture] WARNING: " << path
                          << " uses a mip filter but GL_TEXTURE_MAX_LEVEL=0 — "
                             "texture will be incomplete (black) in WebGL 2!" << std::endl;
            }
        }
    }
#endif

    if (!IsGpuTextureValid(_textureID, _width, _height)) {
        glDeleteTextures(1, &_textureID);
        _textureID = 0;
        if (!allowFallback) {
            throw std::runtime_error("GPU upload produced an incomplete texture (check GL errors / MAX_TEXTURE_SIZE)");
        }
        std::cerr << "[Texture] Warning: GPU upload incomplete for " << path
                  << ". Creating fallback texture." << std::endl;
        CreateFallbackTexture(wrapParam, minFilter, magFilter);
        return;
    }

    std::cout << path << " Loaded" << std::endl;
}

GLuint TextureImage2D::GetTexture() const {
    return _textureID;
}

GLuint TextureImage2D::GetWidth() const {
    return _width;
}

GLuint TextureImage2D::GetHeight() const {
    return _height;
}

void TextureImage2D::ReloadTexture(const std::string& path, GLint wrapParam, GLint minFilter, GLint magFilter) {
    GLuint oldTextureID = _textureID;
    _textureID = 0;

    try {
        LoadTextureFromFile(path, wrapParam, minFilter, magFilter, false);
        // Success: safe to delete the old one now (new one is bound and ready)
        if (oldTextureID != 0) {
            glDeleteTextures(1, &oldTextureID);
        }
    } catch (...) {
        // On any failure during reload (rare on web now that Load catches), restore old texture
        _textureID = oldTextureID;
        if (_textureID == 0) {
            // Last resort: at least have *something* so we don't bind 0
            CreateFallbackTexture(wrapParam, minFilter, magFilter);
        }
    }
}

void TextureImage2D::CreateFallbackTexture(GLint wrapParam, GLint minFilter, GLint magFilter) {
    glGenTextures(1, &_textureID);
    glBindTexture(GL_TEXTURE_2D, _textureID);

    // 4x4 high-contrast checker (gray / dark) so missing textures are obvious but
    // the object still renders and receives lighting instead of solid black.
    uint8_t pixels[4 * 4 * 4];
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            int i = (y * 4 + x) * 4;
            bool checker = ((x + y) % 2) == 0;
            uint8_t v = checker ? 200u : 60u;
            pixels[i + 0] = v;
            pixels[i + 1] = v;
            pixels[i + 2] = v;
            pixels[i + 3] = 255u;
        }
    }
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapParam);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapParam);
    // Force non-mip filters to avoid "incomplete texture" on WebGL even for tiny fallback
    GLint safeMin = (minFilter == GL_LINEAR_MIPMAP_LINEAR || minFilter == GL_LINEAR_MIPMAP_NEAREST ||
                     minFilter == GL_NEAREST_MIPMAP_LINEAR || minFilter == GL_NEAREST_MIPMAP_NEAREST)
                        ? GL_LINEAR : minFilter;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, safeMin);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilter);

#ifdef __EMSCRIPTEN__
    // Explicit single-level declaration for WebGL 2 completeness.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
#endif

    _width = 4;
    _height = 4;
    std::cout << "Created 4x4 fallback texture" << std::endl;
}