// Per-frame (per-eye in XR) values every scene program shares, uploaded once per view by
// Renderer into the uniform buffer at binding 0 (Shader binds the block at link; GLSL ES
// 3.00 has no block `binding`). Layout mirrors FrameUniformData in src/gfx/FrameUniforms.h.
layout(std140) uniform FrameUBO {
    mat4 view;
    mat4 proj;
    mat4 viewProj;
    vec4 cameraPos; // xyz: this eye, render frame
    vec4 sunPos;    // xyz: the Sun, render frame
    float farPlane;
    float zCoef;     // 2.0 / log2(logDepthC * farPlane + 1.0) (common/log_depth.glsl)
    float time;      // seconds since start
    float exposure;  // star HDR exposure
    float logDepthC; // C in log2(1 + C·w): 1 on compressed scale, 1/near on realistic
    float nearPlane;
    float framePad0;
    float framePad1;
} frame;
