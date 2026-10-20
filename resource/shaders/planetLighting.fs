#version 300 es
// Material switches: CatalogMaterial::Defines() picks them per draw from the row's
// render.shaderFlags and which maps are resident (ShaderLibrary caches each combination).
// NEAR_SURFACE (planetLighting.vs) writes depth per fragment.
// permutations: HAS_NIGHT_TEXTURE HAS_SPECULAR_MAP HAS_CLOUDS USE_SPHERE_INTERSECT NEAR_SURFACE
#ifdef NEAR_SURFACE
in float vLogDepth;
#endif
in vec3 vFragPos;
in vec2 vTexCoords;
in vec3 vTangentLightPos;
in vec3 vTangentViewPos;
in vec3 vTangentFragPos;
in vec4 vFragPosLightSpace;

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
uniform vec3 eclipseCasterCenter; // Same space as vFragPos / frame.sunPos.xyz
uniform float eclipseCasterRadius;
uniform float eclipseStarRadius;  // 0.0 => hard-edged disc (Low quality preset)

uniform vec3 ringCenter; // Center of disk in eye space
uniform vec3 ringNormal; // Disk plane normal in eye space
uniform vec2 ringInnerOuterRadiuses; // x = Inner, y = Outer

out vec4 fragColor;

#include "common/frame.glsl"
#include "common/log_depth.glsl"
#include "common/raytrace.glsl"
#include "common/ring.glsl"
#include "common/eclipse.glsl"
#include "common/shadow_pcf.glsl"

float EclipseVisibility() {
    if (!hasEclipseCaster)
        return 1.0;
    return EclipseVisibilityAt(vFragPos, frame.sunPos.xyz, eclipseCasterCenter, eclipseCasterRadius, eclipseStarRadius);
}

float CalculateShadow(vec4 fragPosLightSpace) {
    // Perform perspective divide
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;

    // Transform to [0,1] range
    projCoords = projCoords * 0.5 + 0.5;

    // Get closest depth value from light's perspective (using [0,1] range fragPosLight as coords)
    float closestDepth = texture(shadowMap, projCoords.xy).r;

    // Get depth of current fragment from light's perspective
    float currentDepth = projCoords.z;
    vec3 lightDir = frame.sunPos.xyz - vFragPos;
    vec3 lightDirNorm = normalize(lightDir);

    float shadow = currentDepth - bias > closestDepth ? 1.0 : 0.0;

    if (isNearbyPlanetaryRing) {
#ifdef USE_SPHERE_INTERSECT
        // Behind the parent planet with rings (to avoid shadow from the ring)
        if (intersectSphereAhead(vFragPos, lightDirNorm, parentPlanetCenter, parentPlanetRadiusSquared))
            return 0.0;
#endif

        float u;
        if (RingCrossing(vFragPos, lightDirNorm, lightDirNorm, ringCenter, ringNormal, ringInnerOuterRadiuses, u)) {
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

void main() {
#ifdef NEAR_SURFACE
    gl_FragDepth = LogFragDepth(vLogDepth); // first: every path that is not discarded writes it
#endif
    vec3 diffuseColor = texture(mainDiffuseTexture, vTexCoords).rgb;

    vec3 normal = texture(normalMap, vTexCoords).rgb;
    normal = normalize(normal * 2.0 - 1.0);

    vec3 lightDir = normalize(vTangentLightPos - vTangentFragPos);

    float NdotL = dot(normal, lightDir);

#ifdef HAS_CLOUDS
    vec2 cloudTexCoord = vTexCoords - vec2(yRotation / 360.0, 0.0);
    vec3 cloudColor = texture(cloudTexture, cloudTexCoord).rgb;
    diffuseColor -= cloudColor * 0.5;
#endif

#ifdef HAS_NIGHT_TEXTURE
    vec3 nightColor = texture(nightTexture, vTexCoords).rgb;
    float dayNightAlpha = smoothstep(-0.15, 0.15, NdotL);
    diffuseColor = mix(nightColor, diffuseColor, dayNightAlpha);
#endif

    vec3 ambient = ambientFactor * diffuseColor;

    float diff = max(dot(lightDir, normal), 0.0);
    diffuseColor *= diff;

    float shadow = CalculateShadow(vFragPosLightSpace) * EclipseVisibility();

    if (shadow < 0.05)
        ambient *= 0.1;

    vec3 lighting = ambient + shadow * diffuseColor;

#ifdef HAS_SPECULAR_MAP
    // Blinn-Phong highlight, masked by the map (oceans on Earth) and faded at the terminator.
    vec3 viewDir = normalize(vTangentViewPos - vTangentFragPos);
    vec3 halfwayDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfwayDir), 0.0), 32.0);
    spec *= smoothstep(-0.08, 0.08, NdotL) * 0.675;
    vec4 specularMapColor = texture(specularMap, vTexCoords);
    lighting += shadow * specularMapColor.rrr * specularMapColor.a * spec * starGlowTint;
#endif

    fragColor = vec4(lighting * uSurfaceDim, 1.0);
}
