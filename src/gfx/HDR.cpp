#include "HDR.h"
#include "GlCapabilities.h"
#include "../core/Log.h"

HDR::HDR(const Shader& shader, uint16_t width, uint16_t height, bool enabled)
    : _hdrShader(&shader), _enabled(enabled), _width(width), _height(height) {
    InitQuadBuffers();
    if (_enabled) {
        InitFBO(width, height);
    }
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
    if (!_enabled || !_colorBuffer) {
        return;
    }

    _hdrShader->Use();
    glBindTextureUnit(0, _colorBuffer.get());
    _hdrShader->SetBool("hdr", true);
    _hdrShader->SetFloat("exposure", exposure);
    _hdrShader->SetFloat("gamma", gamma);

    glBindVertexArray(_quadVao.get());
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
}

GLuint HDR::GetHdrFBO() const {
    return _hdrFrameBuffer.get();
}

void HDR::DestroyFBO() {
    _hdrFrameBuffer.reset();
    _colorBuffer.reset();
    _rboDepth.reset();
}

void HDR::InitQuadBuffers() {
    constexpr float quadVertices[] = {
             // positions           // texture Coords
            -1.0f,  1.0f, 0.0f,     0.0f, 1.0f,
            -1.0f, -1.0f, 0.0f,     0.0f, 0.0f,
             1.0f,  1.0f, 0.0f,     1.0f, 1.0f,
             1.0f, -1.0f, 0.0f,     1.0f, 0.0f
    };

    _quadVao = GlVertexArray::Create();
    _quadVbo = GlBuffer::Create();

    glBindVertexArray(_quadVao.get());
    glBindBuffer(GL_ARRAY_BUFFER, _quadVbo.get());
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
}

void HDR::InitFBO(uint16_t width, uint16_t height) {
    // Renderable float/half-float color attachments are an extension on WebGL 2
    // (EXT_color_buffer_float). Without it the RGBA16F attachment below silently
    // yields an incomplete FBO and every subsequent draw into it is dropped.
    if (!GetGlCapabilities().colorBufferFloat) {
        LOG_WARN("HDR", "Renderable float color buffers unsupported "
                        "(no EXT_color_buffer_float); disabling HDR");
        _enabled = false;
        return;
    }

    // Never assume FBO 0 is the default target: under WebXR the XRWebGLLayer
    // framebuffer is bound while we run, so restore whatever was bound on entry.
    GLint previousFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFbo);

    _hdrFrameBuffer = GlFramebuffer::Create();

    _colorBuffer = GlTexture::Create();
    glBindTexture(GL_TEXTURE_2D, _colorBuffer.get());
#ifdef __EMSCRIPTEN__
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
#else
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
#endif
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    _rboDepth = GlRenderbuffer::Create();
    glBindRenderbuffer(GL_RENDERBUFFER, _rboDepth.get());
#ifdef __EMSCRIPTEN__
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
#else
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, width, height);
#endif

    glBindFramebuffer(GL_FRAMEBUFFER, _hdrFrameBuffer.get());
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _colorBuffer.get(), 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, _rboDepth.get());

    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFbo));
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        LOG_ERROR("HDR", "Framebuffer incomplete (0x%x); disabling HDR", status);
        DestroyFBO();
        _enabled = false;
    }
}
