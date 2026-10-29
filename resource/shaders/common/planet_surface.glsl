// The catalog planet material (CatalogMaterial::BindMaterial): diffuse, normal map, night,
// specular and fake cloud shadows, lit by the Sun through the component's shadow map, ring
// shadows and the eclipse umbra decal. planetLighting.fs draws the sphere with it and
// terrain.fs the terrain tiles that replace the sphere close up (docs/ARCHITECTURE.md §11.3),
// so the handoff between them is the same material and the same lighting.
//
// The includer defines, before the include:
//     vec4 SurfaceTexture(sampler2D map, vec2 uv)  // texture() on the sphere; terrain passes
//                                                   // gradients that do not jump at the seam
// and, for relief to bend the normal map, SURFACE_RELIEF.
// Switches it reads: HAS_NIGHT_TEXTURE HAS_SPECULAR_MAP HAS_CLOUDS USE_SPHERE_INTERSECT
#include "common/frame.glsl"
#include "common/raytrace.glsl"
#include "common/ring.glsl"
#include "common/eclipse.glsl"
#include "common/shadow_pcf.glsl"

layout(binding = TU_DIFFUSE) uniform sampler2D mainDiffuseTexture;
layout(binding = TU_NORMAL) uniform sampler2D normalMap;
#ifdef HAS_CLOUDS
layout(binding = TU_CLOUDS) uniform sampler2D cloudTexture;
#endif
#ifdef HAS_NIGHT_TEXTURE
layout(binding = TU_NIGHT) uniform sampler2D nightTexture;
#endif
#ifdef HAS_SPECULAR_MAP
layout(binding = TU_SPECULAR) uniform sampler2D specularMap;
#endif
layout(binding = TU_RING) uniform sampler2D ringDiffuse;
layout(binding = TU_SHADOW_MAP) uniform sampler2D shadowMap;

uniform vec3 starGlowTint;

uniform float ambientFactor;
uniform float bias; // For shadows, in shadow-map depth units
uniform vec2 shadowDepthRange; // this component's light frustum: near, far - near (scene units)
uniform float yRotation; // For fake cloud shadows
uniform float uSurfaceDim;

uniform bool isNearbyPlanetaryRing;

#ifdef USE_SPHERE_INTERSECT
// A moon of a ringed planet: no ring shadow while the planet itself is in the way.
uniform vec3 parentPlanetCenter; // Center of parent planet with planetary ring in eye space
uniform float parentPlanetRadiusSquared;
#endif

// Eclipse umbra decal: one sphere occluding the star, projected onto this surface in the
// lighting pass. No extra FBO and no geometry shader — see Application::ConfigureMainPlanetShader.
uniform bool hasEclipseCaster;
uniform vec3 eclipseCasterCenter; // Same space as the fragment position / frame.sunPos.xyz
uniform float eclipseCasterRadius;
uniform float eclipseStarRadius;  // 0.0 => hard-edged disc (Low quality preset)

uniform vec3 ringCenter; // Center of disk in eye space
uniform vec3 ringNormal; // Disk plane normal in eye space
uniform vec2 ringInnerOuterRadiuses; // x = Inner, y = Outer

float EclipseVisibility(vec3 fragPos) {
    if (!hasEclipseCaster)
        return 1.0;
    return EclipseVisibilityAt(fragPos, frame.sunPos.xyz, eclipseCasterCenter, eclipseCasterRadius, eclipseStarRadius);
}

float CalculateShadow(vec4 fragPosLightSpace, vec3 fragPos) {
    // Perform perspective divide
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;

    // Transform to [0,1] range
    projCoords = projCoords * 0.5 + 0.5;

    // Get closest depth value from light's perspective (using [0,1] range fragPosLight as coords)
    float closestDepth = texture(shadowMap, projCoords.xy).r;

    // Get depth of current fragment from light's perspective
    float currentDepth = projCoords.z;
    vec3 lightDir = frame.sunPos.xyz - fragPos;
    vec3 lightDirNorm = normalize(lightDir);

    float shadow = currentDepth - bias > closestDepth ? 1.0 : 0.0;

    if (isNearbyPlanetaryRing) {
#ifdef USE_SPHERE_INTERSECT
        // Behind the parent planet with rings (to avoid shadow from the ring)
        if (intersectSphereAhead(fragPos, lightDirNorm, parentPlanetCenter, parentPlanetRadiusSquared))
            return 0.0;
#endif

        float u;
        if (RingCrossing(fragPos, lightDirNorm, lightDirNorm, ringCenter, ringNormal, ringInnerOuterRadiuses, u)) {
            // If some planet obscures the ring
            if (shadow > 0.0 && length(frame.sunPos.xyz - ringCenter) - (shadowDepthRange.x + closestDepth * shadowDepthRange.y) > ringInnerOuterRadiuses.y) {
                // PCF won't work, because physically in the place where the penumbra from the PCF should be, there will be a shadow from the ring, and not from the planet
                return 1.0 - shadow;
            }

            // Very high quality shadow from the ring with alpha blending
            return 1.0 - RingOpacity(texture(ringDiffuse, vec2(u, 0.0)));
        }
    }

    return ApplyPCF(shadowMap, projCoords, currentDepth - bias);
}

// The lit surface colour at `uv`. `lightDir` and `viewDir` are unit vectors in the surface's
// tangent frame (east, north, up); `relief` is the terrain's own normal in that frame, which
// SURFACE_RELIEF folds into the normal map (on the bare sphere it is (0, 0, 1) and changes
// nothing). `fragPos` is the render-frame position the shadow and eclipse tests use.
vec3 ShadePlanetSurface(vec2 uv, vec3 relief, vec3 lightDir, vec3 viewDir, vec3 fragPos, vec4 fragPosLightSpace) {
    vec3 diffuseColor = SurfaceTexture(mainDiffuseTexture, uv).rgb;

    vec3 normal = SurfaceTexture(normalMap, uv).rgb;
    normal = normalize(normal * 2.0 - 1.0);
#ifdef SURFACE_RELIEF
    // Whiteout blend: the map's detail rides on the relief's slope.
    normal = normalize(vec3(normal.xy + relief.xy, normal.z * relief.z));
#endif

    float NdotL = dot(normal, lightDir);

#ifdef HAS_CLOUDS
    vec2 cloudTexCoord = uv - vec2(yRotation / 360.0, 0.0);
    vec3 cloudColor = SurfaceTexture(cloudTexture, cloudTexCoord).rgb;
    diffuseColor -= cloudColor * 0.5;
#endif

#ifdef HAS_NIGHT_TEXTURE
    vec3 nightColor = SurfaceTexture(nightTexture, uv).rgb;
    float dayNightAlpha = smoothstep(-0.15, 0.15, NdotL);
    diffuseColor = mix(nightColor, diffuseColor, dayNightAlpha);
#endif

    vec3 ambient = ambientFactor * diffuseColor;

    float diff = max(dot(lightDir, normal), 0.0);
    diffuseColor *= diff;

    float shadow = CalculateShadow(fragPosLightSpace, fragPos) * EclipseVisibility(fragPos);

    if (shadow < 0.05)
        ambient *= 0.1;

    vec3 lighting = ambient + shadow * diffuseColor;

#ifdef HAS_SPECULAR_MAP
    // Blinn-Phong highlight, masked by the map (oceans on Earth) and faded at the terminator.
    vec3 halfwayDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfwayDir), 0.0), 32.0);
    spec *= smoothstep(-0.08, 0.08, NdotL) * 0.675;
    vec4 specularMapColor = SurfaceTexture(specularMap, uv);
    lighting += shadow * specularMapColor.rrr * specularMapColor.a * spec * starGlowTint;
#endif

    return lighting * uSurfaceDim;
}
