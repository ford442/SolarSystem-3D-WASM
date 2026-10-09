#version 300 es
in vec3 vFragPos;
in vec2 vTexCoords;
in vec3 vTangentLightPos;
in vec3 vTangentViewPos;
in vec3 vTangentFragPos;
in vec4 vFragPosLightSpace;

uniform sampler2D mainDiffuseTexture;
uniform sampler2D cloudTexture;
uniform sampler2D nightTexture;
uniform sampler2D normalMap;
uniform sampler2D specularMap;
uniform sampler2D ringDiffuse;
uniform sampler2D shadowMap;

uniform vec3 lightPos;
uniform vec3 viewPos;
uniform vec3 starGlowTint;

uniform float farPlane;
uniform float ambientFactor;
uniform float bias; // For shadows
uniform float yRotation; // For fake cloud shadows
uniform float uSurfaceDim;

uniform bool hasNightTexture;
uniform bool hasSpecularMap;
uniform bool hasSpecular;
uniform bool hasClouds;
uniform bool isNearbyPlanetaryRing;
uniform bool isUseSphereIntersect; // To avoid the ring shadow while behind a planet (parent planet with rings)

uniform vec3 parentPlanetCenter; // Center of parent planet with planetary ring in eye space
uniform float parentPlanetRadiusSquared;

// Eclipse umbra decal: one sphere occluding the star, projected onto this surface in the
// lighting pass. No extra FBO and no geometry shader — see Application::ConfigureMainPlanetShader.
uniform bool hasEclipseCaster;
uniform vec3 eclipseCasterCenter; // Same space as vFragPos / lightPos
uniform float eclipseCasterRadius;
uniform float eclipseStarRadius;  // 0.0 => hard-edged disc (Low quality preset)

uniform vec3 ringCenter; // Center of disk in eye space
uniform vec3 ringNormal; // Disk plane normal in eye space
uniform vec2 ringInnerOuterRadiuses; // x = Inner, y = Outer

out vec4 fragColor;

#include "common/raytrace.glsl"
#include "common/ring.glsl"
#include "common/eclipse.glsl"
#include "common/shadow_pcf.glsl"

float EclipseVisibility() {
    if (!hasEclipseCaster)
        return 1.0;
    return EclipseVisibilityAt(vFragPos, lightPos, eclipseCasterCenter, eclipseCasterRadius, eclipseStarRadius);
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
    vec3 lightDir = lightPos - vFragPos;
    vec3 lightDirNorm = normalize(lightDir);

    float shadow = currentDepth - bias > closestDepth ? 1.0 : 0.0;

    if (isNearbyPlanetaryRing) {
        // Behind the parent planet with rings (to avoid shadow from the ring)
        if (isUseSphereIntersect && intersectSphereAhead(vFragPos, lightDirNorm, parentPlanetCenter, parentPlanetRadiusSquared))
            return 0.0;

        float u;
        if (RingCrossing(vFragPos, lightDirNorm, lightDirNorm, ringCenter, ringNormal, ringInnerOuterRadiuses, u)) {
            // If some planet obscures the ring
            if (shadow > 0.0 && length(lightPos - ringCenter) - closestDepth * farPlane > ringInnerOuterRadiuses.y) {
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
    vec3 diffuseColor, specular;

    diffuseColor = texture(mainDiffuseTexture, vTexCoords).rgb;

    vec3 normal = texture(normalMap, vTexCoords).rgb;
    normal = normalize(normal * 2.0 - 1.0);

    vec3 lightDir = normalize(vTangentLightPos - vTangentFragPos);

    float NdotL = dot(normal, lightDir);

    if (hasClouds) {
        vec2 cloudTexCoord = vTexCoords - vec2(yRotation / 360.0, 0.0);
        vec3 cloudColor = texture(cloudTexture, cloudTexCoord).rgb;
        diffuseColor -= cloudColor * 0.5;
    }

    if (hasNightTexture) {
        vec3 nightColor = texture(nightTexture, vTexCoords).rgb;
        float dayNightAlpha = smoothstep(-0.15, 0.15, NdotL);
        diffuseColor = mix(nightColor, diffuseColor, dayNightAlpha);
    }

    vec3 ambient = ambientFactor * diffuseColor;

    float diff = max(dot(lightDir, normal), 0.0);
    diffuseColor *= diff;

    float spec;
    if (hasSpecular) {
        vec3 viewDir = normalize(vTangentViewPos - vTangentFragPos);
        vec3 reflectDir = reflect(-lightDir, normal);
        vec3 halfwayDir = normalize(lightDir + viewDir);
        spec = pow(max(dot(normal, halfwayDir), 0.0), 32.0);
        float specularMix = smoothstep(-0.08, 0.08, NdotL);
        spec = mix(0.0, spec, specularMix) * 0.675;
    }

    if (hasSpecularMap) {
        vec4 specularMapColor = texture(specularMap, vTexCoords);
        specular = specularMapColor.rrr * specularMapColor.a * spec * starGlowTint;
    }
    else {
        specular = spec * starGlowTint;
    }

    float shadow = CalculateShadow(vFragPosLightSpace) * EclipseVisibility();

    if (shadow < 0.05)
        ambient *= 0.1;

    vec3 lighting;
    if (hasSpecular)
        lighting = ambient + shadow * (diffuseColor + specular);
    else
        lighting = ambient + shadow * diffuseColor;

    fragColor = vec4(lighting * uSurfaceDim, 1.0);
}
