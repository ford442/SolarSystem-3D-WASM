#version 300 es
uniform sampler2D ringTexture;
uniform sampler2D shadowMap;

uniform vec3 lightPos;
uniform vec3 planetPos;
uniform vec3 starGlowTint;

uniform float planetRadius;
uniform float zCoef;
uniform float bias; // For shadows
uniform float uSurfaceDim;

uniform vec3 camPos;

in vec3 fPosition;
in vec3 normal;
in vec4 fragPosLightSpace;
in vec2 texCoords;
in vec3 fWorldPosition;
in float fLogZ;

in vec3 vEyePos;
in vec3 vLight0Pos;

vec3 sphereIntersectHitpoint;
vec3 sphereIntersectNormal;
float sphereIntersectDistance;

out vec4 fragColor;

#include "common/shadow_pcf.glsl"

float vecSum(vec3 v) {
    return v.x + v.y + v.z;
}

// https://github.com/RegrowthStudios/SoAGameData/blob/64cce77f42e7bf0136b513957ef7aa53e8ddbfbf/Shaders/PlanetRings/Rings.frag
float sphereIntersect(vec3 raydir, vec3 rayorig, vec3 pos, float rad) {
    float a = vecSum(raydir*raydir);
    float b = vecSum(raydir * (2.0 * (rayorig - pos)));
    float c = vecSum(pos*pos) + vecSum(rayorig*rayorig) - 2.0 * vecSum(rayorig*pos) - rad * rad;
    float D = b * b + (-4.0) * a * c;

    // If ray can not intersect then stop
    if (D < 0.0) {
        return 0.0;
    }

    D = sqrt(D);

    // Ray can intersect the sphere, solve the closer hitpoint
    float t = (-0.5) * (b + D) / a;
    if (t > 0.0) {
        sphereIntersectDistance = sqrt(a)*t;
        sphereIntersectHitpoint = rayorig + t * raydir;
        sphereIntersectNormal = (sphereIntersectHitpoint - pos) / rad;
    } else {
        return 1.9;
    }
    return 4.0;
}

float sphereIntersectAmount(vec3 raydir, vec3 rayorig, vec3 pos, float rad) {
    float sphereDistance = dot(raydir, pos) - dot(raydir, rayorig);
    if (sphereDistance > 0.0)
        return 0.0;
    vec3 closestPoint = sphereDistance * raydir + rayorig;
    float distanceToSphere = length(closestPoint - pos);
    return distanceToSphere - rad;
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
    vec3 lightDir = normalize(lightPos - fWorldPosition);

    return ApplyPCF(shadowMap, projCoords, currentDepth - bias);
}

void main() {
    gl_FragDepth = log2(fLogZ) * zCoef * 0.5;

    vec4 ringColor = texture(ringTexture, texCoords);

    if (ringColor.w == 0.0)
        discard;

    const float smoothingAmount = 0.0001;
    float shadow = clamp(-sphereIntersectAmount(normalize(fWorldPosition - lightPos), fWorldPosition, planetPos, planetRadius) / smoothingAmount, 0.0, 1.0);

    if (shadow > 0.0) { // The ring is obscured by the parent planet
        ringColor.rgb *= 1.0 - shadow;
        fragColor = vec4(ringColor.rgb * uSurfaceDim, ringColor.a);
        return;
    }

    float NdotL = dot(normal, normalize(lightPos - planetPos));

    shadow = CalculateShadow(fragPosLightSpace);
    ringColor.rgb *= shadow;

    if (NdotL < 0.0 && shadow != 0.0) {
        // Calculate back light function (Mie scattering)
        float light0Dist = length(vLight0Pos);
        vec3  light0Dir  = vLight0Pos / light0Dist;

        float eyeDist = length(vEyePos);
        vec3  eyeDir  = vEyePos / eyeDist;

        float Cos0 = dot(light0Dir, eyeDir);
        const float g = -0.9;
        const float g2 = g * g;
        const float k = 1.5 / 806.202 * ((1.0 - g2) / (2.0 + g2));
        float backLit0 = 12.0 * k * (1.0 + Cos0 * Cos0) * pow(1.0 + g2 - 2.0 * g * Cos0, -1.5);
        ringColor.rgb *= backLit0 * starGlowTint * starGlowTint; // Star glow tint^2
    }

    fragColor = vec4(ringColor.rgb * uSurfaceDim, ringColor.a);
}