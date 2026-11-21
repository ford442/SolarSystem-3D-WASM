#ifndef SOLARSYSTEM_TEXTUREIMAGE2D_H
#define SOLARSYSTEM_TEXTUREIMAGE2D_H
#include "GlHandle.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>

class TextureBuild;

class TextureImage2D {
public:
    TextureImage2D() = default;
    explicit TextureImage2D(const std::string& path, GLint wrapParam = GL_REPEAT,
                            GLint minFilter = GL_LINEAR_MIPMAP_LINEAR, GLint magFilter = GL_LINEAR);
    // Owns its GL texture, so move-only: a copy would delete the name out from under the
    // other copy (LensFlare once held one by value and ReloadTexture freed the original).
    // A move also re-points a pending ReloadTextureAsync at the new address.
    TextureImage2D(TextureImage2D&& other) noexcept
        : _slot(std::move(other._slot)), _texture(std::move(other._texture)), _width(other._width),
          _height(other._height) {
        if (_slot) {
            _slot->owner = this;
        }
    }
    TextureImage2D& operator=(TextureImage2D&& other) noexcept {
        if (this != &other) {
            if (_slot) {
                _slot->owner = nullptr;
            }
            _slot = std::move(other._slot);
            _texture = std::move(other._texture);
            _width = other._width;
            _height = other._height;
            if (_slot) {
                _slot->owner = this;
            }
        }
        return *this;
    }
    ~TextureImage2D() {
        if (_slot) {
            _slot->owner = nullptr; // a pending async reload finds no one and drops its texture
        }
    }
    TextureImage2D(const TextureImage2D&) = delete;
    TextureImage2D& operator=(const TextureImage2D&) = delete;
    GLuint GetTexture() const;
    GLuint GetWidth() const;
    GLuint GetHeight() const;
    /** Decode and upload now, on this thread. Supersedes a pending ReloadTextureAsync. */
    void ReloadTexture(const std::string& path, GLint wrapParam = GL_REPEAT, GLint minFilter = GL_LINEAR_MIPMAP_LINEAR,
                       GLint magFilter = GL_LINEAR);

    enum class AsyncResult {
        Uploaded,   // the new texture is bound in place of the old one
        Failed,     // decode or upload failed; the old texture stays
        Superseded, // a later reload (or `stillWanted` returning false, or destruction) won
    };
    /**
     * Reload without stalling the frame: the bytes are read here, decoded in a job
     * (core/JobSystem.h), and uploaded by Application::PumpUploads under the per-frame
     * byte budget (assets/UploadQueue.h), in row bands if the texture is larger than one
     * frame's budget. The old texture is drawn until the new one is complete. `done` runs on
     * the main thread, once. `stillWanted`, if given, is asked just before the swap.
     */
    void ReloadTextureAsync(const std::string& path, std::function<void(AsyncResult)> done,
                            std::function<bool()> stillWanted = nullptr, GLint wrapParam = GL_REPEAT,
                            GLint minFilter = GL_LINEAR_MIPMAP_LINEAR, GLint magFilter = GL_LINEAR);
    /** Web: anisotropic filtering ceiling for textures loaded from now on (mobile GPUs get 4). */
    static void SetWebAnisotropyCap(float cap);

private:
    /** What a pending async reload holds instead of `this`, so a move or destruction is safe. */
    struct AsyncSlot {
        TextureImage2D* owner = nullptr;
        std::uint64_t generation = 0;
    };

    static float s_webAnisotropyCap;
    std::shared_ptr<AsyncSlot> _slot;
    GlTexture _texture;
    GLuint _width = 0, _height = 0;

    void LoadTextureFromFile(const std::string& path, GLint wrapParam, GLint minFilter, GLint magFilter,
                             bool allowFallback = true);
    void CreateFallbackTexture(GLint wrapParam, GLint minFilter, GLint magFilter);
    void Adopt(TextureBuild& build);
    friend class TextureBuild;
};

#endif //SOLARSYSTEM_TEXTUREIMAGE2D_H
