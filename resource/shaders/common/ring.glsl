// Planetary ring crossing along a ray, shared by the shadow, flare and glow shaders.
// Requires common/raytrace.glsl.
//
// `sideReference` picks which face of the ring the ray is tested against: the ring normal is
// flipped to point the same way as it (the light direction for shadows, the star's offset
// from the ring for the flare/glow). Returns true when the ray crosses the ring band between
// the inner and outer radius, with `u` the radial texture coordinate in [0, 1].
bool RingCrossing(vec3 origin, vec3 dir, vec3 sideReference,
                  vec3 ringCenter, vec3 ringNormal, vec2 ringInnerOuterRadiuses, out float u) {
    u = 0.0;
    vec3 orientedNormal = dot(ringNormal, sideReference) < 0.0 ? -ringNormal : ringNormal;

    float hitDistance;
    if (!intersectDisk(orientedNormal, ringCenter, ringInnerOuterRadiuses.y, origin, dir, hitDistance))
        return false;
    if (hitDistance <= ringInnerOuterRadiuses.x)
        return false;

    u = (hitDistance - ringInnerOuterRadiuses.x) / (ringInnerOuterRadiuses.y - ringInnerOuterRadiuses.x);
    return true;
}

// How much light the ring texel blocks, as the lighting shaders have always measured it.
float RingOpacity(vec4 ringColor) {
    return (ringColor.r + ringColor.g + ringColor.b) * ringColor.a;
}
