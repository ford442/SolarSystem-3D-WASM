// Logarithmic depth: every scene object shares one depth buffer from a metre above a surface
// out past the Kuiper belt, which a linear z cannot resolve (docs/ARCHITECTURE.md §11.1):
//
//     depth = log2(1 + C·w) / log2(1 + C·far),   frame.zCoef = 2 / log2(1 + C·far)
//
// C is frame.logDepthC: 1 on compressed scale (the curve it always used), 1/near on
// realistic, so the curve's linear toe stays inside the near plane.
#include "common/frame.glsl"

// 1 + C·w, what the fragment form interpolates: it is linear in w, so a perspective-correct
// varying of it is exact at every fragment.
float LogDepthArgument(float clipW) {
    return frame.logDepthC * clipW + 1.0;
}

// Vertex form, applied after projection; multiplying by w undoes the perspective divide for z.
// Interpolated z is only right while a triangle is small next to its distance, and clipping
// goes wrong for one that crosses the eye plane: close to a surface, the NEAR_SURFACE
// permutations keep the projection's own z and write LogFragDepth instead.
vec4 ApplyLogDepth(vec4 clipPosition) {
    clipPosition.z = log2(max(1e-6, LogDepthArgument(clipPosition.w))) * frame.zCoef - 1.0;
    clipPosition.z *= clipPosition.w;
    return clipPosition;
}

// Fragment form, vertex half: returns the LogDepthArgument to interpolate, and parks clip z
// at -frame.nearPlane. x, y and w are untouched, so clipping stays linear, and the clip
// volume's near face is w = frame.nearPlane (the navigation's altitude-driven near) whatever
// near the projection was built with — WebXR fixes its own. Not z = 0: that puts the near
// face on the eye plane, and a triangle clipped there gets vertices at w = 0 whose divide
// leaves holes. Depth then comes only from LogFragDepth.
float PrepareLogFragDepth(inout vec4 clipPosition) {
    float argument = LogDepthArgument(clipPosition.w);
    clipPosition.z = -frame.nearPlane;
    return argument;
}

// Fragment form: gl_FragDepth for an interpolated LogDepthArgument, the window-space value of
// the vertex form's z.
float LogFragDepth(float logDepthArgument) {
    return log2(max(1e-6, logDepthArgument)) * frame.zCoef * 0.5;
}
