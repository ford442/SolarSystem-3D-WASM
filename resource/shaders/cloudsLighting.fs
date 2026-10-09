#version 300 es
in vec3 vFragPos;
in vec2 vTexCoords;
in vec3 vTangentLightPos;
in vec3 vTangentViewPos;
in vec3 vTangentFragPos;
in vec4 vFragPosLightSpace;

uniform sampler2D mainDiffuseTexture;
uniform sampler2D cloudsNormalMap;
uniform sampler2D shadowMap;

uniform vec3 lightPos;
uniform vec3 viewPos;

uniform float farPlane;
uniform float ambientFactor;
uniform float bias; // For shadows
uniform float uSurfaceDim;
// uniform bool isNearbyPlanetaryRing; // Not used in the scene, but if desired, it can be implemented as in shader planet.fs

out vec4 fragColor;

#define PCF_NUM_SAMPLES 2.0 // lower to increase fps, higher to increase softening
#include "common/shadow_pcf.glsl"

float CalculateShadow(vec4 fragPosLightSpace) {
    // Perform perspective divide
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;

    // Transform to [0,1] range
    projCoords = projCoords * 0.5 + 0.5;

    // Get closest depth value from light's perspective (using [0,1] range fragPosLight as coords)
    float closestDepth = texture(shadowMap, projCoords.xy).r;

    // Get depth of current fragment from light's perspective
    float currentDepth = projCoords.z;

    return ApplyPCF(shadowMap, projCoords, currentDepth - bias);
}

void main() {
    vec3 diffuseColor = texture(mainDiffuseTexture, vTexCoords).rgb;

    vec3 normal = texture(cloudsNormalMap, vTexCoords).rgb;
    normal = normalize(normal * 2.0 - 1.0);

    vec3 lightDir = normalize(vTangentLightPos - vTangentFragPos);

    float NdotL = dot(normal, lightDir);
    vec3 color = diffuseColor;

    float ambientAlpha = smoothstep(-0.15, 0.25, NdotL);
    float ambientMult = mix(0.01, ambientFactor, ambientAlpha);
    vec3 ambient = ambientMult * color;

    vec3 diffuse;

    float diff = max(dot(lightDir, normal), 0.0);
    diffuse = diff * color;

    float shadow = CalculateShadow(vFragPosLightSpace);

    if (shadow < 0.05)
        ambient *= 0.1;

    vec3 lighting = ambient + shadow * diffuse;
    fragColor = vec4(lighting * uSurfaceDim, 1.0f);
}