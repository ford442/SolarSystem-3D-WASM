#include "TextureImage2D.h"
#include "WebResourceFetcher.h"
#include <cstdint>
#include <algorithm>

TextureImage2D::TextureImage2D(const std::string& path, GLint wrapParam, GLint minFilter, GLint magFilter) {
    LoadTextureFromFile(path, wrapParam, minFilter, magFilter);
}

void TextureImage2D::LoadTextureFromFile(const std::string& path, GLint wrapParam, GLint minFilter, GLint magFilter) {
    // Fetch texture on demand (no-op on native; async/sync download on web into MEMFS)
    WebResourceFetcher::Fetch(path);

    glGenTextures(1, &_textureID);
    glBindTexture(GL_TEXTURE_2D, _textureID);

    bool isCompressed = false;
    bool hasEmbeddedMipmaps = false;

    try {
        // [PORTING NOTE]
        // Ensure files are preloaded (emcc --preload-file) or fetched asynchronously.
        // For preloaded files, std::ifstream in CDDSImage will work transparently.
        CDDSImage image;
        image.load(path, false);
        isCompressed = image.is_compressed();
        hasEmbeddedMipmaps = (image.get_num_mipmaps() > 0);
        image.upload_texture2D();
        _width = image.get_width();
        _height = image.get_height();
    }
    catch (const std::runtime_error& error) {
#ifdef __EMSCRIPTEN__
        // On web, many moon/ring/high-res textures are intentionally omitted from the
        // repository and deployment. Use a visible fallback so planets/moons never
        // appear solid black and the scene remains stable.
        std::cerr << "[Texture] Warning: Failed to load " << path << " (" << error.what()
                  << "). Creating fallback texture." << std::endl;
        // Delete the incomplete texture object before creating fallback
        glDeleteTextures(1, &_textureID);
        _textureID = 0;
        CreateFallbackTexture(wrapParam, minFilter, magFilter);
        return;
#else
        throw std::runtime_error("Image " + path + " cannot be loaded");
#endif
    }

    // S3TC compressed textures (DXT1/3/5) do not support glGenerateMipmap in WebGL 2.
    // Mipmaps must be pre-embedded in the DDS file. Only attempt generation for
    // uncompressed textures that have no embedded mipmaps.
    if (!isCompressed) {
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
#ifndef __EMSCRIPTEN__
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
        LoadTextureFromFile(path, wrapParam, minFilter, magFilter);
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

    _width = 4;
    _height = 4;
    std::cout << "Created 4x4 fallback texture" << std::endl;
}