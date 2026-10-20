#ifndef SOLARSYSTEM_TEXTUREIMAGE2D_H
#define SOLARSYSTEM_TEXTUREIMAGE2D_H
#include "../../src/3rdparty/nv_dds.h"
#include "../gfx/GlHandle.h"
#include <iostream>
#include <string>
#include <mutex>
#include <memory>
#include <thread>

using namespace nv_dds;

class TextureImage2D {
public:
    TextureImage2D() = default;
    explicit TextureImage2D(const std::string& path, GLint wrapParam = GL_REPEAT, GLint minFilter = GL_LINEAR_MIPMAP_LINEAR, GLint magFilter = GL_LINEAR);
    // Owns its GL texture, so move-only: a copy would delete the name out from under the
    // other copy (LensFlare once held one by value and ReloadTexture freed the original).
    TextureImage2D(TextureImage2D&&) noexcept = default;
    TextureImage2D& operator=(TextureImage2D&&) noexcept = default;
    TextureImage2D(const TextureImage2D&) = delete;
    TextureImage2D& operator=(const TextureImage2D&) = delete;
    GLuint GetTexture() const;
    GLuint GetWidth() const;
    GLuint GetHeight() const;
    void ReloadTexture(const std::string& path, GLint wrapParam = GL_REPEAT, GLint minFilter = GL_LINEAR_MIPMAP_LINEAR, GLint magFilter = GL_LINEAR);
    /** Web: anisotropic filtering ceiling for textures loaded from now on (mobile GPUs get 4). */
    static void SetWebAnisotropyCap(float cap);

private:
    static float s_webAnisotropyCap;
    GlTexture _texture;
    GLuint _width = 0, _height = 0;

    void LoadTextureFromFile(const std::string& path, GLint wrapParam, GLint minFilter, GLint magFilter, bool allowFallback = true);
    void CreateFallbackTexture(GLint wrapParam, GLint minFilter, GLint magFilter);
};

#endif //SOLARSYSTEM_TEXTUREIMAGE2D_H
