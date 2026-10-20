#include "GlCapabilities.h"
#include <GL/glew.h>
#include "../core/Log.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#ifdef __EMSCRIPTEN__
// GLFW's own glfwExtensionSupported() (Emscripten's library_glfw.js) never converts its
// const char* argument to a JS string before calling Array.includes() on it, so it always
// compares a raw pointer against a string array and never matches. Query the WebGL context
// directly instead.
bool IsWebGlExtensionSupported(const char* name) {
    return EM_ASM_INT({
        try {
            const exts = GLctx.getSupportedExtensions() || [];
            return exts.includes(UTF8ToString($0)) ? 1 : 0;
        } catch (e) {
            return 0;
        }
    }, name) != 0;
}
#endif

namespace {
GlCapabilities ProbeGlCapabilities() {
    GlCapabilities caps;

#ifdef __EMSCRIPTEN__
    caps.anisotropicFiltering = IsWebGlExtensionSupported("EXT_texture_filter_anisotropic");
    caps.s3tcCompressedTextures = IsWebGlExtensionSupported("WEBGL_compressed_texture_s3tc");
    caps.bptcCompressedTextures = IsWebGlExtensionSupported("EXT_texture_compression_bptc");
    caps.etc2CompressedTextures = IsWebGlExtensionSupported("WEBGL_compressed_texture_etc");
    caps.astcCompressedTextures = IsWebGlExtensionSupported("WEBGL_compressed_texture_astc");
    caps.colorBufferFloat = IsWebGlExtensionSupported("EXT_color_buffer_float");
    // WebGL extensions are off until requested; getExtension() is what turns it on.
    caps.parallelShaderCompile = EM_ASM_INT({
        try {
            return GLctx.getExtension('KHR_parallel_shader_compile') ? 1 : 0;
        } catch (e) {
            return 0;
        }
    }) != 0;
#else
    // Desktop GL 4.6 core always has these via GLEW; anisotropy is applied unconditionally
    // in TextureImage2D.cpp for native builds, so this cap is web-only for now.
    caps.anisotropicFiltering = true;
    caps.s3tcCompressedTextures = true;
    caps.bptcCompressedTextures = true;
    // ETC2 and ASTC are nominally core in GL 4.3+ but are software-decoded by desktop
    // drivers. Leave them off so the desktop path keeps picking a BC format.
    caps.etc2CompressedTextures = false;
    caps.astcCompressedTextures = false;
    caps.colorBufferFloat = true;
    // 0xFFFFFFFF = as many compiler threads as the driver likes.
    if (GLEW_KHR_parallel_shader_compile) {
        glMaxShaderCompilerThreadsKHR(0xFFFFFFFFu);
        caps.parallelShaderCompile = true;
    } else if (GLEW_ARB_parallel_shader_compile) {
        glMaxShaderCompilerThreadsARB(0xFFFFFFFFu);
        caps.parallelShaderCompile = true;
    }
#endif

    if (caps.anisotropicFiltering) {
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &caps.maxAnisotropy);
    }

    GLint maxTextureSize = 2048;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
    caps.maxTextureSize = maxTextureSize;

    return caps;
}
}

const GlCapabilities& GetGlCapabilities() {
    static const GlCapabilities caps = [] {
        GlCapabilities c = ProbeGlCapabilities();
        const auto yesNo = [](bool value) { return value ? "yes" : "no"; };
        LOG_INFO("GlCapabilities",
                 "anisotropy=%s (max %gx) | S3TC=%s | BPTC=%s | ETC2=%s | ASTC=%s | preferredPack=%s"
                 " | colorBufferFloat=%s | parallelShaderCompile=%s | maxTextureSize=%d",
                 yesNo(c.anisotropicFiltering), c.maxAnisotropy, yesNo(c.s3tcCompressedTextures),
                 yesNo(c.bptcCompressedTextures), yesNo(c.etc2CompressedTextures), yesNo(c.astcCompressedTextures),
                 c.PreferredTexturePack(), yesNo(c.colorBufferFloat), yesNo(c.parallelShaderCompile),
                 c.maxTextureSize);
        return c;
    }();
    return caps;
}
