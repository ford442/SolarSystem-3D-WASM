#pragma once

// Not declared by Emscripten's GLES3 headers or its GL/glew.h shim (and <GLES2/gl2ext.h>
// conflicts with that shim's own typedefs when included alongside it). These are stable,
// spec-fixed enum values, safe to define directly.
#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#endif
#ifndef GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif

#include "TextureFormatSupport.h"

/**
 * GPU/driver capabilities probed once against the current GL context. Must be called
 * after glfwMakeContextCurrent(); the result is cached for the process lifetime (a WebGL
 * context is never recreated, and native builds target a fixed desktop GL profile).
 */
struct GlCapabilities {
    bool anisotropicFiltering = false;
    float maxAnisotropy = 1.0f;
    bool s3tcCompressedTextures = false;
    // Compressed-texture families beyond S3TC. Safari/iOS and most Android GPUs expose
    // none of the BC formats, so the loader needs these to pick a format they can
    // actually accept instead of falling through to the checkerboard.
    bool bptcCompressedTextures = false;   // EXT_texture_compression_bptc (BC7)
    bool etc2CompressedTextures = false;   // WEBGL_compressed_texture_etc
    bool astcCompressedTextures = false;   // WEBGL_compressed_texture_astc
    bool colorBufferFloat = false;
    int maxTextureSize = 2048;

    TextureFormats::FormatCapabilities ToFormatCapabilities() const {
        return {s3tcCompressedTextures, bptcCompressedTextures,
                etc2CompressedTextures, astcCompressedTextures};
    }

    /** Convenience wrapper over TextureFormats::PreferredPackName for these caps. */
    const char* PreferredTexturePack() const {
        return TextureFormats::PreferredPackName(ToFormatCapabilities());
    }
};

const GlCapabilities& GetGlCapabilities();
