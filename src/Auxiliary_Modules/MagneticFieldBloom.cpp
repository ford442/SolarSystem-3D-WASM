#include "MagneticFieldBloom.h"

#include <algorithm>
#include <iostream>

namespace {

constexpr float kQuadVertices[] = {
    -1.0f, 1.0f,  0.0f, 0.0f, 1.0f, -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
    1.0f,  1.0f,  0.0f, 1.0f, 1.0f, 1.0f,  -1.0f, 0.0f, 1.0f, 0.0f,
};

void HalfRes(uint16_t width, uint16_t height, uint16_t& outW, uint16_t& outH) {
    outW = static_cast<uint16_t>(std::max(1, static_cast<int>(width) / 2));
    outH = static_cast<uint16_t>(std::max(1, static_cast<int>(height) / 2));
}

} // namespace

MagneticFieldBloom::MagneticFieldBloom(uint16_t width, uint16_t height, bool enabled)
    : _enabled(enabled), _fullWidth(width), _fullHeight(height) {
    InitQuad();
    _blurShader = std::make_unique<Shader>("resource/shaders/passThrough.vs",
                                           "resource/shaders/magneticFieldBloomBlur.fs");
    _compositeShader = std::make_unique<Shader>("resource/shaders/passThrough.vs",
                                                "resource/shaders/magneticFieldBloomComposite.fs");
    if (_enabled) {
        InitTargets(width, height);
    }
}

MagneticFieldBloom::~MagneticFieldBloom() {
    DestroyTargets();
    if (_quadVbo) {
        glDeleteBuffers(1, &_quadVbo);
        _quadVbo = 0;
    }
    if (_quadVao) {
        glDeleteVertexArrays(1, &_quadVao);
        _quadVao = 0;
    }
}

void MagneticFieldBloom::SetEnabled(bool enabled, uint16_t width, uint16_t height) {
    if (enabled == _enabled && (!_enabled || (width == _fullWidth && height == _fullHeight))) {
        return;
    }
    _enabled = enabled;
    _fullWidth = width;
    _fullHeight = height;
    DestroyTargets();
    if (_enabled) {
        InitTargets(width, height);
    }
}

void MagneticFieldBloom::Resize(uint16_t width, uint16_t height) {
    if (!_enabled) {
        _fullWidth = width;
        _fullHeight = height;
        return;
    }
    if (width == _fullWidth && height == _fullHeight) {
        return;
    }
    _fullWidth = width;
    _fullHeight = height;
    DestroyTargets();
    InitTargets(width, height);
}

void MagneticFieldBloom::BeginCapture() {
    if (!_enabled || _capture.fbo == 0) {
        return;
    }

    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &_prevFbo);
    glGetIntegerv(GL_VIEWPORT, _prevViewport);

    glBindFramebuffer(GL_FRAMEBUFFER, _capture.fbo);
    glViewport(0, 0, _width, _height);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (_capture.depth != 0 && _prevViewport[2] > 0 && _prevViewport[3] > 0) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(_prevFbo));
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _capture.fbo);
        glBlitFramebuffer(_prevViewport[0], _prevViewport[1], _prevViewport[0] + _prevViewport[2],
                          _prevViewport[1] + _prevViewport[3], 0, 0, _width, _height, GL_DEPTH_BUFFER_BIT,
                          GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, _capture.fbo);
    }
}

void MagneticFieldBloom::BlurAndComposite(int passes, float intensity) {
    if (!_enabled || _capture.color == 0 || !_blurShader || !_compositeShader) {
        return;
    }

    const int iterations = std::max(1, passes);
    unsigned int src = _capture.color;

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);

    _blurShader->Use();
    _blurShader->SetInt("uImage", 0);

    for (int i = 0; i < iterations; ++i) {
        glBindFramebuffer(GL_FRAMEBUFFER, _ping.fbo);
        glViewport(0, 0, _width, _height);
        glBindTextureUnit(0, src);
        _blurShader->SetVec2("uDirection", glm::vec2(1.0f / static_cast<float>(_width), 0.0f));
        DrawQuad();

        glBindFramebuffer(GL_FRAMEBUFFER, _pong.fbo);
        glBindTextureUnit(0, _ping.color);
        _blurShader->SetVec2("uDirection", glm::vec2(0.0f, 1.0f / static_cast<float>(_height)));
        DrawQuad();
        src = _pong.color;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(_prevFbo));
    glViewport(_prevViewport[0], _prevViewport[1], _prevViewport[2], _prevViewport[3]);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glDisable(GL_DEPTH_TEST);

    _compositeShader->Use();
    _compositeShader->SetInt("uImage", 0);
    _compositeShader->SetFloat("uIntensity", std::max(0.0f, intensity));
    glBindTextureUnit(0, src);
    DrawQuad();
}

void MagneticFieldBloom::InitQuad() {
    glGenVertexArrays(1, &_quadVao);
    glGenBuffers(1, &_quadVbo);
    glBindVertexArray(_quadVao);
    glBindBuffer(GL_ARRAY_BUFFER, _quadVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kQuadVertices), kQuadVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                          reinterpret_cast<void*>(3 * sizeof(float)));
    glBindVertexArray(0);
}

void MagneticFieldBloom::InitTargets(uint16_t width, uint16_t height) {
    HalfRes(width, height, _width, _height);
    CreateColorTarget(_capture, _width, _height, true);
    CreateColorTarget(_ping, _width, _height, false);
    CreateColorTarget(_pong, _width, _height, false);

    if (_capture.fbo == 0 || _ping.fbo == 0 || _pong.fbo == 0) {
        std::cout << "[MagneticField] Bloom FBOs incomplete; disabling bloom" << std::endl;
        DestroyTargets();
        _enabled = false;
    }
}

void MagneticFieldBloom::DestroyTargets() {
    DestroyTarget(_capture);
    DestroyTarget(_ping);
    DestroyTarget(_pong);
    _width = 0;
    _height = 0;
}

void MagneticFieldBloom::CreateColorTarget(Target& target, uint16_t width, uint16_t height, bool withDepth) {
    DestroyTarget(target);

    glGenFramebuffers(1, &target.fbo);
    glGenTextures(1, &target.color);
    glBindTexture(GL_TEXTURE_2D, target.color);
#ifdef __EMSCRIPTEN__
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
#else
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
#endif
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindFramebuffer(GL_FRAMEBUFFER, target.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target.color, 0);

    if (withDepth) {
        glGenRenderbuffers(1, &target.depth);
        glBindRenderbuffer(GL_RENDERBUFFER, target.depth);
#ifdef __EMSCRIPTEN__
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
#else
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, width, height);
#endif
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, target.depth);
    }

    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        DestroyTarget(target);
    }
}

void MagneticFieldBloom::DestroyTarget(Target& target) {
    if (target.depth) {
        glDeleteRenderbuffers(1, &target.depth);
        target.depth = 0;
    }
    if (target.color) {
        glDeleteTextures(1, &target.color);
        target.color = 0;
    }
    if (target.fbo) {
        glDeleteFramebuffers(1, &target.fbo);
        target.fbo = 0;
    }
}

void MagneticFieldBloom::DrawQuad() const {
    glBindVertexArray(_quadVao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
}
