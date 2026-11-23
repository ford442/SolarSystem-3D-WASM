#ifndef SOLARSYSTEM_HDR_H
#define SOLARSYSTEM_HDR_H
#include "Shader.h"
#include "GlHandle.h"

class HDR : public IGpuRestorable {
public:
    explicit HDR(const Shader& shader, uint16_t width, uint16_t height, bool enabled = true);
    HDR(const HDR&) = delete;
    HDR& operator=(const HDR&) = delete;
    /** After a lost context: the quad, and the float target again if HDR was on. */
    void RestoreGpu() override;
    void SetEnabled(bool enabled, uint16_t width, uint16_t height);
    bool IsEnabled() const;
    void Resize(uint16_t width, uint16_t height);
    void Render(float exposure, float gamma) const;
    GLuint GetHdrFBO() const;

private:
    const Shader* _hdrShader;
    bool _enabled = true;
    uint16_t _width = 0;
    uint16_t _height = 0;
    GlVertexArray _quadVao;
    GlBuffer _quadVbo;
    GlFramebuffer _hdrFrameBuffer;
    GlTexture _colorBuffer;
    GlRenderbuffer _rboDepth;

    void InitQuadBuffers();
    void DestroyFBO();
    void InitFBO(uint16_t width, uint16_t height);
};

#endif //SOLARSYSTEM_HDR_H
