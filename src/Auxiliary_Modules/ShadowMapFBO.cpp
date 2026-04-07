#include "ShadowMapFBO.h"

ShadowMapFBO::ShadowMapFBO(uint16_t shadowMapWidth, uint16_t shadowMapHeight) : _shadowMapWidth(shadowMapWidth), _shadowMapHeight(shadowMapHeight) {
    InitFBO();
}

ShadowMapFBO::~ShadowMapFBO() {
    if (_frameBuffer != 0) {
        glDeleteFramebuffers(1, &_frameBuffer);
        _frameBuffer = 0;
    }
    if (_shadowMap != 0) {
        glDeleteTextures(1, &_shadowMap);
        _shadowMap = 0;
    }
}

uint16_t ShadowMapFBO::GetShadowMapWidth() const {
    return _shadowMapWidth;
}

uint16_t ShadowMapFBO::GetShadowMapHeight() const {
    return _shadowMapHeight;
}

GLuint ShadowMapFBO::GetShadowMap() const {
    return _shadowMap;
}

GLuint ShadowMapFBO::GetFBO() const {
    return _frameBuffer;
}

void ShadowMapFBO::InitFBO() {
    glGenFramebuffers(1, &_frameBuffer);

    glGenTextures(1, &_shadowMap);
    glBindTexture(GL_TEXTURE_2D, _shadowMap);
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

    glBindFramebuffer(GL_FRAMEBUFFER, _frameBuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, _shadowMap, 0);
#ifdef __EMSCRIPTEN__
    GLenum drawBuffers[] = { GL_NONE };
    glDrawBuffers(1, drawBuffers);
#else
    glDrawBuffer(GL_NONE);
#endif
    glReadBuffer(GL_NONE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}