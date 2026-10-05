/**
 * Fraction of the star still reaching `position` past an eclipse caster, in [0, 1].
 *
 * Treats the caster as a sphere on the segment between the point and the star centre.
 * Inside the geometric umbra radius the star is fully blocked; out to the penumbra radius
 * it fades, both from similar triangles on the shadow cone. starRadius = 0 gives a
 * hard-edged disc (Low quality preset).
 *
 * The caller passes radii already converted into the caster's own orbital scale (see
 * Renderer::ConfigureEclipseUmbra), so the cone here is close to the real one even
 * though the planet under it is drawn several times oversized. The planet's exaggerated
 * radius is the remaining error and it makes the shadow track across the disc faster than
 * life; it does not change whether the eclipse happens, which is decided on the CPU at
 * true scale.
 */
float EclipseVisibilityAt(vec3 position, vec3 lightPosition, vec3 casterCenter, float casterRadius, float starRadius) {
    vec3 toLight = lightPosition - position;
    float lightDist = length(toLight);
    if (lightDist < 1e-4)
        return 1.0;
    vec3 lightDirNorm = toLight / lightDist;

    vec3 toCaster = casterCenter - position;
    float along = dot(toCaster, lightDirNorm);
    // Caster behind this point, or past the star: it cannot shadow us.
    if (along <= 0.0 || along >= lightDist)
        return 1.0;

    float miss = length(toCaster - lightDirNorm * along);

    // Shadow cone cross-section at the caster's distance from the point.
    float spread = along / max(lightDist - along, 1e-4);
    float umbra = max(casterRadius - (starRadius - casterRadius) * spread, 0.0);
    float penumbra = casterRadius + (starRadius + casterRadius) * spread;
    penumbra = max(penumbra, umbra + 1e-4);

    return smoothstep(umbra, penumbra, miss);
}
