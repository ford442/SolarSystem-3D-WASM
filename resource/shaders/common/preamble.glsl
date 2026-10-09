// Inserted by ShaderSource right after #version in every program (source string 1).
// Defines available here and in every shader: SS_STAGE_VERTEX / SS_STAGE_FRAGMENT and
// SS_GLSL_ES (WebGL 2) / SS_GLSL_DESKTOP (native 4.60 core).
//
// Keep this to precision defaults. No helper macros: shaders declare their own constants
// (atmosphere.fs has `const float PI`), and a #define here would collide with them.
// A shader that wants less precision still says `precision mediump float;` itself; the
// later declaration wins.
#ifdef SS_STAGE_FRAGMENT
precision highp float;
precision highp int;
precision highp sampler2D;
precision highp sampler3D;
#endif
