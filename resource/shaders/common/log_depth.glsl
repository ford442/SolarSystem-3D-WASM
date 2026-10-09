// Logarithmic depth: every scene object shares one depth buffer from the Moon's surface to
// Neptune, which a linear z cannot resolve. zCoef = 2.0 / log2(farPlane + 1.0).
// Applied after projection; multiplying by w undoes the perspective divide for z.
vec4 ApplyLogDepth(vec4 clipPosition, float zCoef) {
    clipPosition.z = log2(max(1e-6, clipPosition.w + 1.0)) * zCoef - 1.0;
    clipPosition.z *= clipPosition.w;
    return clipPosition;
}
