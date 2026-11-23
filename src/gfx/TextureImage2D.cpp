#include "TextureImage2D.h"
#include "../assets/TextureDecode.h"
#include "../assets/UploadQueue.h"
#include "../assets/WebResourceFetcher.h"
#include "../core/JobSystem.h"
#include "../core/Log.h"
#include "GlCapabilities.h"
#include "GlPlatform.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "GpuResourceRegistry.h"

namespace {
/** The upload completed but the driver reports no level 0 (MAX_TEXTURE_SIZE, a lost context). */
class IncompleteTextureError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

bool UsesMipmaps(GLint minFilter) {
    return minFilter == GL_NEAREST_MIPMAP_NEAREST || minFilter == GL_LINEAR_MIPMAP_NEAREST ||
           minFilter == GL_NEAREST_MIPMAP_LINEAR || minFilter == GL_LINEAR_MIPMAP_LINEAR;
}

GLint FullChainLevels(std::uint32_t width, std::uint32_t height) {
    std::uint32_t maxDim = std::max(width, height);
    GLint levels = 1;
    while (maxDim > 1) {
        maxDim >>= 1;
        ++levels;
    }
    return levels;
}

// width/height are only read on the web path; native asks the driver instead.
bool IsGpuTextureValid(GLuint textureId, [[maybe_unused]] unsigned int width, [[maybe_unused]] unsigned int height) {
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

/** GlCapabilities as the (GL-free) decoder needs it. Main thread: it reads the probe. */
TextureDecodeTarget CurrentDecodeTarget() {
    const GlCapabilities& caps = GetGlCapabilities();
    TextureDecodeTarget target;
    target.formats = caps.ToFormatCapabilities();
    target.maxTextureSize = caps.maxTextureSize;
#ifdef __EMSCRIPTEN__
    target.webGl = true;
#endif
    return target;
}

void ClearGlErrors() {
    while (glGetError() != GL_NO_ERROR) {
    }
}

/** UNPACK_ALIGNMENT 1 for the scope: payload rows are tightly packed. */
class TightUnpack {
public:
    TightUnpack() {
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &_previous);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    }
    ~TightUnpack() { glPixelStorei(GL_UNPACK_ALIGNMENT, _previous); }
    TightUnpack(const TightUnpack&) = delete;
    TightUnpack& operator=(const TightUnpack&) = delete;

private:
    GLint _previous = 4;
};
} // namespace

/**
 * The GL half of a texture load: a TexturePayload (decoded in a job, assets/TextureDecode.h)
 * becomes a finished GL texture, in one call or across frames.
 *
 * A payload that fits the step's byte budget uploads level by level with glTexImage2D, as
 * the loader always has (a rejected mip caps the chain). A larger one allocates immutable
 * storage (glTexStorage2D) and fills it in row bands with glTex[Compressed]SubImage2D, a
 * budget's worth per step, so a 4K software-decoded map no longer costs one frame 64 MB of
 * upload. The texture is only handed to its owner once every level is in.
 */
class TextureBuild {
public:
    TextureBuild(TexturePayload payload, GLint wrapParam, GLint minFilter, GLint magFilter)
        : _payload(std::move(payload)), _wrap(wrapParam), _minFilter(minFilter), _magFilter(magFilter) {}

    /** Upload up to `budget` bytes (at least one band). Throws std::runtime_error on GL failure. */
    UploadQueue::Step Advance(std::size_t budget) {
        UploadQueue::Step step;
        step.finished = false;
        if (_phase == Phase::Bands && !texture) {
            // The context was lost between bands (GpuResources::InvalidateAll emptied the
            // texture): the payload is still here, so start over on the new context.
            _phase = Phase::Start;
            _level = 0;
            _row = 0;
        }
        if (_phase == Phase::Start) {
            texture = GlTexture::Create();
            glBindTexture(GL_TEXTURE_2D, texture.get());
            width = _payload.width;
            height = _payload.height;
            if (_payload.ByteSize() <= budget || !CanBand()) {
                step.bytes = UploadAllLevels();
                Finish();
                step.finished = true;
                return step;
            }
            AllocateStorage();
            _phase = Phase::Bands;
        }
        glBindTexture(GL_TEXTURE_2D, texture.get());
        step.bytes = UploadBands(budget);
        if (_level >= _payload.levels.size()) {
            _hasMipmaps = _payload.levels.size() > 1;
            _allowGenerate = _payload.allowGenerateMipmap && _payload.levels.size() == 1;
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, static_cast<GLint>(_payload.levels.size()) - 1);
            Finish();
            step.finished = true;
        }
        return step;
    }

    const std::string& Path() const { return _payload.path; }
    GLint Wrap() const { return _wrap; }
    GLint MinFilter() const { return _minFilter; }
    GLint MagFilter() const { return _magFilter; }

    GlTexture texture;
    GLuint width = 0;
    GLuint height = 0;

private:
    enum class Phase { Start, Bands, Done };

    /** Banding needs immutable storage, whose compressed level 0 must be whole blocks. */
    bool CanBand() const {
        if (_payload.internalFormat == 0) {
            return false;
        }
        if (_payload.compressed) {
            return _payload.width % _payload.blockWidth == 0 && _payload.height % _payload.blockHeight == 0;
        }
        return true;
    }

    std::size_t UploadAllLevels() {
        ClearGlErrors();
        const TightUnpack unpack;
        std::size_t bytes = 0;
        int uploaded = 0;
        for (std::size_t level = 0; level < _payload.levels.size(); ++level) {
            const TexturePayload::Level& entry = _payload.levels[level];
            auto levelWidth = static_cast<GLsizei>(entry.width);
            auto levelHeight = static_cast<GLsizei>(entry.height);
            if (level == 0 && _payload.padBaseToBlock) {
                // WebGL 2 wants a DXT base level of whole 4x4 blocks; a 1024x1 level is the
                // same bytes as 1024x4, so declare the padded size.
                levelWidth = std::max(levelWidth, 4);
                levelHeight = std::max(levelHeight, 4);
            }
            if (_payload.compressed) {
                glCompressedTexImage2D(GL_TEXTURE_2D, static_cast<GLint>(level),
                                       static_cast<GLenum>(_payload.internalFormat), levelWidth, levelHeight, 0,
                                       static_cast<GLsizei>(entry.size), _payload.LevelData(level));
            } else {
                glTexImage2D(GL_TEXTURE_2D, static_cast<GLint>(level), static_cast<GLint>(_payload.internalFormat),
                             levelWidth, levelHeight, 0, static_cast<GLenum>(_payload.format),
                             static_cast<GLenum>(_payload.type), _payload.LevelData(level));
            }
            const GLenum error = glGetError();
            if (error != GL_NO_ERROR) {
                if (level == 0) {
                    throw std::runtime_error(_payload.path + ": GL rejected the base level (error 0x" +
                                             std::to_string(static_cast<unsigned>(error)) + ")");
                }
                // A rejected mip is survivable: cap the chain at what actually landed.
                LOG_WARN("Texture", "%s: GL rejected mip %zu (error 0x%x); keeping %d level(s)", _payload.path.c_str(),
                         level, error, uploaded);
                break;
            }
            ++uploaded;
            bytes += entry.size;
        }
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, std::max(uploaded - 1, 0));
        _hasMipmaps = uploaded > 1;
        _allowGenerate = _payload.allowGenerateMipmap && uploaded == 1;
        return bytes;
    }

    void AllocateStorage() {
        const bool generate = _payload.allowGenerateMipmap && _payload.levels.size() == 1;
        _storageLevels =
            generate ? FullChainLevels(_payload.width, _payload.height) : static_cast<GLint>(_payload.levels.size());
        ClearGlErrors();
        glTexStorage2D(GL_TEXTURE_2D, _storageLevels, static_cast<GLenum>(_payload.internalFormat),
                       static_cast<GLsizei>(_payload.width), static_cast<GLsizei>(_payload.height));
        const GLenum error = glGetError();
        if (error != GL_NO_ERROR) {
            throw std::runtime_error(_payload.path + ": glTexStorage2D failed (error 0x" +
                                     std::to_string(static_cast<unsigned>(error)) + ")");
        }
    }

    std::size_t UploadBands(std::size_t budget) {
        ClearGlErrors();
        const TightUnpack unpack;
        std::size_t spent = 0;
        while (_level < _payload.levels.size()) {
            const TexturePayload::Level& entry = _payload.levels[_level];
            // A unit is one row of texels, or one row of blocks for a compressed format.
            const std::uint32_t unitRows = _payload.compressed ? _payload.blockHeight : 1;
            const std::size_t unitBytes = _payload.compressed
                                              ? ((entry.width + _payload.blockWidth - 1) / _payload.blockWidth) *
                                                    std::size_t{_payload.texelOrBlockBytes}
                                              : std::size_t{entry.width} * _payload.texelOrBlockBytes;
            const std::size_t unitsLeft = (entry.height - _row + unitRows - 1) / unitRows;
            std::size_t units = budget > spent ? (budget - spent) / std::max<std::size_t>(unitBytes, 1) : 0;
            if (spent == 0) {
                units = std::max<std::size_t>(units, 1); // always make progress
            }
            if (units == 0) {
                break;
            }
            units = std::min(units, unitsLeft);
            const std::uint32_t rows =
                std::min<std::uint32_t>(static_cast<std::uint32_t>(units) * unitRows, entry.height - _row);
            const std::size_t offset = std::size_t{_row / unitRows} * unitBytes;
            const std::size_t bytes = std::min(units * unitBytes, entry.size - offset);
            const std::uint8_t* data = _payload.LevelData(_level) + offset;
            if (_payload.compressed) {
                glCompressedTexSubImage2D(GL_TEXTURE_2D, static_cast<GLint>(_level), 0, static_cast<GLint>(_row),
                                          static_cast<GLsizei>(entry.width), static_cast<GLsizei>(rows),
                                          static_cast<GLenum>(_payload.internalFormat), static_cast<GLsizei>(bytes),
                                          data);
            } else {
                glTexSubImage2D(GL_TEXTURE_2D, static_cast<GLint>(_level), 0, static_cast<GLint>(_row),
                                static_cast<GLsizei>(entry.width), static_cast<GLsizei>(rows),
                                static_cast<GLenum>(_payload.format), static_cast<GLenum>(_payload.type), data);
            }
            const GLenum error = glGetError();
            if (error != GL_NO_ERROR) {
                throw std::runtime_error(_payload.path + ": band upload of level " + std::to_string(_level) +
                                         " failed (error 0x" + std::to_string(static_cast<unsigned>(error)) + ")");
            }
            spent += bytes;
            _row += rows;
            if (_row >= entry.height) {
                ++_level;
                _row = 0;
            }
        }
        return spent;
    }

    void Finish() {
        _phase = Phase::Done;
        GLint minFilter = _minFilter;
        // Compressed textures (S3TC/ETC2/ASTC/BPTC) do not support glGenerateMipmap in
        // WebGL 2 — their mip chain must come pre-embedded in the container. Only attempt
        // generation for uncompressed uploads that arrived without one.
        if (_allowGenerate && !_hasMipmaps) {
            // glGenerateMipmap fills levels BASE+1..min(chain, MAX_LEVEL), and the upload
            // declared MAX_LEVEL = 0, so open the range first: generating under MAX_LEVEL 0
            // makes nothing (the chain this used to "generate" natively was never there).
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, FullChainLevels(_payload.width, _payload.height) - 1);
            ClearGlErrors();
            glGenerateMipmap(GL_TEXTURE_2D);
            const GLenum err = glGetError();
            if (err != GL_NO_ERROR) {
                LOG_WARN("Texture", "Warning: glGenerateMipmap failed for %s (error 0x%x)", _payload.path.c_str(), err);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
            } else {
                _hasMipmaps = true;
            }
        }

        // If no mipmaps are available (neither generated nor embedded), using any
        // mipmap min-filter makes the texture "incomplete" — degrade to GL_LINEAR.
        if (!_hasMipmaps && UsesMipmaps(minFilter)) {
#ifndef __EMSCRIPTEN__
            LOG_WARN("Texture", "Warning: No mipmaps for %s -- falling back min filter to GL_LINEAR",
                     _payload.path.c_str());
#endif
            minFilter = GL_LINEAR;
        }

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, _wrap);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, _wrap);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, _magFilter);
#ifdef __EMSCRIPTEN__
        const auto& glCaps = GetGlCapabilities();
        if (glCaps.anisotropicFiltering) {
            glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT,
                            std::min(glCaps.maxAnisotropy, TextureImage2D::s_webAnisotropyCap));
        }
        if (UsesMipmaps(minFilter)) {
            GLint maxLevel = 0;
            glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, &maxLevel);
            if (maxLevel == 0) {
                LOG_WARN("Texture",
                         "Warning: %s uses a mip filter but GL_TEXTURE_MAX_LEVEL=0 — "
                         "texture will be incomplete (black) in WebGL 2!",
                         _payload.path.c_str());
            }
        }
#else
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 16);
#endif

        if (!IsGpuTextureValid(texture.get(), width, height)) {
            texture.reset();
            throw IncompleteTextureError("GPU upload produced an incomplete texture for " + _payload.path +
                                         " (check GL errors / MAX_TEXTURE_SIZE)");
        }
    }

    TexturePayload _payload;
    GLint _wrap;
    GLint _minFilter;
    GLint _magFilter;
    Phase _phase = Phase::Start;
    GLint _storageLevels = 0;
    std::size_t _level = 0;
    std::uint32_t _row = 0;
    bool _hasMipmaps = false;
    bool _allowGenerate = false;
};

float TextureImage2D::s_webAnisotropyCap = 8.0f;

void TextureImage2D::SetWebAnisotropyCap(float cap) {
    s_webAnisotropyCap = cap;
}

TextureImage2D::TextureImage2D(const std::string& path, GLint wrapParam, GLint minFilter, GLint magFilter)
    : IGpuRestorable(GpuResources::Phase::Textures) {
    LoadTextureFromFile(path, wrapParam, minFilter, magFilter, true);
}

void TextureImage2D::LoadTextureFromFile(const std::string& path, GLint wrapParam, GLint minFilter, GLint magFilter,
                                         bool allowFallback) {
    // On web the low tier is preloaded into the .data file and mid/high tiers arrive
    // through TextureLoadingQueue's async download, so the file is resident by now;
    // a miss falls through to CreateFallbackTexture() below.
    WebResourceFetcher::RequireResident(path, "TextureImage2D");
    _texture.reset();

    try {
        // Same two halves as ReloadTextureAsync, back to back on this thread.
        TextureBuild build(DecodeTexture(path, ReadTextureFile(path), CurrentDecodeTarget()), wrapParam, minFilter,
                           magFilter);
        build.Advance(std::numeric_limits<std::size_t>::max());
        Adopt(build);
    } catch (const IncompleteTextureError& error) {
        if (!allowFallback) {
            throw;
        }
        LOG_WARN("Texture", "Warning: GPU upload incomplete for %s. Creating fallback texture.", path.c_str());
        CreateFallbackTexture(wrapParam, minFilter, magFilter);
        return;
    } catch (const std::runtime_error& error) {
#ifdef __EMSCRIPTEN__
        if (!allowFallback) {
            throw;
        }
        // On web, many moon/ring/high-res textures are intentionally omitted from the
        // repository and deployment. Use a visible fallback so planets/moons never
        // appear solid black and the scene remains stable.
        LOG_WARN("Texture", "Warning: Failed to load %s (%s). Creating fallback texture.", path.c_str(), error.what());
        CreateFallbackTexture(wrapParam, minFilter, magFilter);
        return;
#else
        throw std::runtime_error("Image " + path + " cannot be loaded (" + error.what() + ")");
#endif
    }

    LOG_DEBUG("Texture", "%s Loaded", path.c_str());
}

void TextureImage2D::Adopt(TextureBuild& build) {
    _texture = std::move(build.texture);
    _width = build.width;
    _height = build.height;
    _source = Source{build.Path(), build.Wrap(), build.MinFilter(), build.MagFilter(), false};
}

void TextureImage2D::RestoreGpu() {
    if (_slot && _slot->settled != _slot->generation) {
        // A reload is still decoding or uploading; it lands on the new context (TextureBuild
        // restarts a banded upload whose texture was lost) and is newer than _source anyway.
        return;
    }
    if (_source.fallback) {
        CreateFallbackTexture(_source.wrap, _source.minFilter, _source.magFilter);
        return;
    }
    if (_source.path.empty()) {
        return;
    }
    // Async like any reload: a scene's worth of 4K maps decodes in jobs and uploads under the
    // per-frame byte budget instead of stalling the first restored frame. A failure keeps the
    // (empty) texture; the next LOD change or restore tries again.
    const Source source = _source;
    ReloadTextureAsync(source.path, nullptr, nullptr, source.wrap, source.minFilter, source.magFilter);
}

GLuint TextureImage2D::GetTexture() const {
    return _texture.get();
}

GLuint TextureImage2D::GetWidth() const {
    return _width;
}

GLuint TextureImage2D::GetHeight() const {
    return _height;
}

void TextureImage2D::ReloadTexture(const std::string& path, GLint wrapParam, GLint minFilter, GLint magFilter) {
    if (_slot) {
        ++_slot->generation; // a pending async reload must not land on top of this one
        _slot->settled = _slot->generation; // and this one finishes before returning
    }
    // The old texture stays alive until the new one is bound and ready, then goes with `previous`.
    GlTexture previous = std::move(_texture);
    const GLuint previousWidth = _width, previousHeight = _height;

    try {
        LoadTextureFromFile(path, wrapParam, minFilter, magFilter, false);
    } catch (const std::exception& error) {
        // On any failure during reload (rare on web now that Load catches), restore old texture
        LOG_WARN("Texture", "Warning: Reloading %s failed (%s); keeping the previous texture", path.c_str(),
                 error.what());
        _texture = std::move(previous);
        _width = previousWidth;
        _height = previousHeight;
        if (!_texture) {
            // Last resort: at least have *something* so we don't bind 0
            CreateFallbackTexture(wrapParam, minFilter, magFilter);
        }
    }
}

void TextureImage2D::ReloadTextureAsync(const std::string& path, std::function<void(AsyncResult)> done,
                                        std::function<bool()> stillWanted, GLint wrapParam, GLint minFilter,
                                        GLint magFilter) {
    if (!_slot) {
        _slot = std::make_shared<AsyncSlot>();
        _slot->owner = this;
    }
    const std::uint64_t generation = ++_slot->generation;
    const std::shared_ptr<AsyncSlot> slot = _slot;
    const auto finish = [done = std::move(done), slot, generation](AsyncResult result) {
        if (slot->generation == generation) {
            slot->settled = generation;
        }
        if (done) {
            done(result);
        }
    };

    // The bytes are read here: MEMFS is the main thread's (a worker's file I/O would be
    // proxied back to it), and the read is a memcpy next to the decode.
    WebResourceFetcher::RequireResident(path, "TextureImage2D");
    std::vector<std::uint8_t> bytes;
    try {
        bytes = ReadTextureFile(path);
    } catch (const std::exception& error) {
        LOG_WARN("Texture", "Warning: Reloading %s failed (%s); keeping the previous texture", path.c_str(),
                 error.what());
        finish(AsyncResult::Failed);
        return;
    }

    auto decoded = std::make_shared<Jobs::Future<TexturePayload>>(
        Jobs::Submit([path, bytes = std::move(bytes), target = CurrentDecodeTarget()] {
            return DecodeTexture(path, bytes, target);
        }));
    auto build = std::make_shared<std::unique_ptr<TextureBuild>>();

    UploadQueue::Get().Push(
        // Once the payload is taken (Get() consumes the future) the build is what is ready.
        path, [decoded, build] { return *build != nullptr || decoded->Ready(); },
        [=](std::size_t budget) -> UploadQueue::Step {
            const auto superseded = [&] {
                return slot->owner == nullptr || slot->generation != generation || (stillWanted && !stillWanted());
            };
            if (superseded()) {
                finish(AsyncResult::Superseded);
                return {};
            }
            try {
                if (!*build) {
                    *build = std::make_unique<TextureBuild>(decoded->Get(), wrapParam, minFilter, magFilter);
                }
                const UploadQueue::Step step = (*build)->Advance(budget);
                if (!step.finished) {
                    return step;
                }
                slot->owner->Adopt(**build);
                LOG_DEBUG("Texture", "%s Loaded", path.c_str());
                finish(AsyncResult::Uploaded);
                return step;
            } catch (const std::exception& error) {
                LOG_WARN("Texture", "Warning: Reloading %s failed (%s); keeping the previous texture", path.c_str(),
                         error.what());
                finish(AsyncResult::Failed);
                return {};
            }
        });
}

void TextureImage2D::CreateFallbackTexture(GLint wrapParam, GLint minFilter, GLint magFilter) {
    _source = Source{"", wrapParam, minFilter, magFilter, true};
    _texture = GlTexture::Create();
    glBindTexture(GL_TEXTURE_2D, _texture.get());

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
                        ? GL_LINEAR
                        : minFilter;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, safeMin);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilter);

#ifdef __EMSCRIPTEN__
    // Explicit single-level declaration for WebGL 2 completeness.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
#endif

    _width = 4;
    _height = 4;
    LOG_WARN("Texture", "Created 4x4 fallback texture");
}