#include "TextureImage2D.h"
#include "WebResourceFetcher.h"

TextureImage2D::TextureImage2D(const std::string& path, GLint wrapParam, GLint minFilter, GLint magFilter) {
    LoadTextureFromFile(path, wrapParam, minFilter, magFilter);
}

void TextureImage2D::LoadTextureFromFile(const std::string& path, GLint wrapParam, GLint minFilter, GLint magFilter) {
    // Fetch texture on demand
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
        throw std::runtime_error("Image " + path + " cannot be loaded");
    }

    // S3TC compressed textures (DXT1/3/5) do not support glGenerateMipmap in WebGL 2.
    // Mipmaps must be pre-embedded in the DDS file. Only attempt generation for
    // uncompressed textures that have no embedded mipmaps.
    if (!isCompressed) {
        glGenerateMipmap(GL_TEXTURE_2D);
        GLenum err = glGetError();
        if (err != GL_NO_ERROR) {
            std::cerr << "Warning: glGenerateMipmap failed for " << path
                      << " (error 0x" << std::hex << err << std::dec << ")" << std::endl;
        } else {
            hasEmbeddedMipmaps = true;
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
            std::cerr << "Warning: No mipmaps for " << path
                      << " -- falling back min filter to GL_LINEAR" << std::endl;
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
    // Delete the old texture if it exists
    if (_textureID != 0) {
        glDeleteTextures(1, &_textureID);
        _textureID = 0;
    }
    
    // Load the new texture (LoadTextureFromFile throws on failure)
    LoadTextureFromFile(path, wrapParam, minFilter, magFilter);
}