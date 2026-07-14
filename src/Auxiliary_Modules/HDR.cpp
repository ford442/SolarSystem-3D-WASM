#include "HDR.h"

HDR::HDR(const Shader& shader, uint16_t width, uint16_t height, bool enabled)
    : _hdrShader(&shader), _enabled(enabled), _width(width), _height(height) {
    InitQuadBuffers();
    if (_enabled) {
        InitFBO(width, height);
    }
}

HDR::~HDR() {
    if (_quadVao != 0) {
        glDeleteVertexArrays(1, &_quadVao);
        _quadVao = 0;
    }
    if (_quadVbo != 0) {
        glDeleteBuffers(1, &_quadVbo);
        _quadVbo = 0;
    }
    DestroyFBO();
}

void HDR::SetEnabled(bool enabled, uint16_t width, uint16_t height) {
    if (enabled == _enabled && (!_enabled || (width == _width && height == _height))) {
        return;
    }

    _enabled = enabled;
    _width = width;
    _height = height;
    DestroyFBO();
    if (_enabled) {
        InitFBO(width, height);
    }
}

bool HDR::IsEnabled() const {
    return _enabled;
}

void HDR::Resize(uint16_t width, uint16_t height) {
    if (!_enabled) {
        _width = width;
        _height = height;
        return;
    }
    if (width == _width && height == _height) {
        return;
    }
    _width = width;
    _height = height;
    DestroyFBO();
    InitFBO(width, height);
}

void HDR::Render(float exposure, float gamma) const {
    if (!_enabled || _colorBuffer == 0) {
        return;
    }

    _hdrShader->Use();
    _hdrShader->SetInt("hdrBuffer", 0);
    glBindTextureUnit(0, _colorBuffer);
    _hdrShader->SetBool("hdr", true);
    _hdrShader->SetFloat("exposure", exposure);
    _hdrShader->SetFloat("gamma", gamma);

    glBindVertexArray(_quadVao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
}

GLuint HDR::GetHdrFBO() const {
    return _hdrFrameBuffer;
}

void HDR::DestroyFBO() {
    if (_hdrFrameBuffer != 0) {
        glDeleteFramebuffers(1, &_hdrFrameBuffer);
        _hdrFrameBuffer = 0;
    }
    if (_colorBuffer != 0) {
        glDeleteTextures(1, &_colorBuffer);
        _colorBuffer = 0;
    }
    if (_rboDepth != 0) {
        glDeleteRenderbuffers(1, &_rboDepth);
        _rboDepth = 0;
    }
}

void HDR::InitQuadBuffers() {
    constexpr float quadVertices[] = {
             // positions           // texture Coords
            -1.0f,  1.0f, 0.0f,     0.0f, 1.0f,
            -1.0f, -1.0f, 0.0f,     0.0f, 0.0f,
             1.0f,  1.0f, 0.0f,     1.0f, 1.0f,
             1.0f, -1.0f, 0.0f,     1.0f, 0.0f
    };

    glGenVertexArrays(1, &_quadVao);
    glGenBuffers(1, &_quadVbo);

    glBindVertexArray(_quadVao);
    glBindBuffer(GL_ARRAY_BUFFER, _quadVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
}

void HDR::InitFBO(uint16_t width, uint16_t height) {
    glGenFramebuffers(1, &_hdrFrameBuffer);

    glGenTextures(1, &_colorBuffer);
    glBindTexture(GL_TEXTURE_2D, _colorBuffer);
#ifdef __EMSCRIPTEN__
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
#else
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
#endif
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glGenRenderbuffers(1, &_rboDepth);
    glBindRenderbuffer(GL_RENDERBUFFER, _rboDepth);
#ifdef __EMSCRIPTEN__
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
#else
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, width, height);
#endif

    glBindFramebuffer(GL_FRAMEBUFFER, _hdrFrameBuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _colorBuffer, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, _rboDepth);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
