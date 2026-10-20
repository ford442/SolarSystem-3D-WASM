#include "ShadowMapFBO.h"
#include "../core/Log.h"

ShadowMapFBO::ShadowMapFBO(uint16_t shadowMapWidth, uint16_t shadowMapHeight) : _shadowMapWidth(shadowMapWidth), _shadowMapHeight(shadowMapHeight) {
    InitFBO();
}

void ShadowMapFBO::Resize(uint16_t shadowMapWidth, uint16_t shadowMapHeight) {
    if (shadowMapWidth == _shadowMapWidth && shadowMapHeight == _shadowMapHeight) {
        return;
    }
    _shadowMapWidth = shadowMapWidth;
    _shadowMapHeight = shadowMapHeight;
    DestroyFBO();
    InitFBO();
}

void ShadowMapFBO::DestroyFBO() {
    _frameBuffer.reset();
    _shadowMap.reset();
}

uint16_t ShadowMapFBO::GetShadowMapWidth() const {
    return _shadowMapWidth;
}

uint16_t ShadowMapFBO::GetShadowMapHeight() const {
    return _shadowMapHeight;
}

GLuint ShadowMapFBO::GetShadowMap() const {
    return _shadowMap.get();
}

GLuint ShadowMapFBO::GetFBO() const {
    return _frameBuffer.get();
}

void ShadowMapFBO::InitFBO() {
    _frameBuffer = GlFramebuffer::Create();

    _shadowMap = GlTexture::Create();
    glBindTexture(GL_TEXTURE_2D, _shadowMap.get());
#ifdef __EMSCRIPTEN__
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, _shadowMapWidth, _shadowMapHeight, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
#else
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32, _shadowMapWidth, _shadowMapHeight, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);

    constexpr float borderColor[] = {1.0, 1.0, 1.0, 1.0};
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);
#endif

    GLint previousFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFbo);

    glBindFramebuffer(GL_FRAMEBUFFER, _frameBuffer.get());
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, _shadowMap.get(), 0);
#ifdef __EMSCRIPTEN__
    GLenum drawBuffers[] = { GL_NONE };
    glDrawBuffers(1, drawBuffers);
#else
    glDrawBuffer(GL_NONE);
#endif
    glReadBuffer(GL_NONE);

    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    // Restore the caller's binding, not FBO 0: in WebXR the default target is the
    // XRWebGLLayer framebuffer and a resize can happen mid-session.
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFbo));
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        LOG_ERROR("Shadows", "Shadow map framebuffer incomplete (0x%x)", status);
    }
}