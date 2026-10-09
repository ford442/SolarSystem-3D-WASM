// Shadow-map sampling with bilinear-filtered PCF.
// https://www.youtube.com/watch?v=yn5UJzMqxj0
//
// Define PCF_NUM_SAMPLES before including to change the kernel width (default 3x3; the
// cloud shell uses 2x2). Returns the lit fraction in [0, 1].
#ifndef PCF_NUM_SAMPLES
#define PCF_NUM_SAMPLES 3.0
#endif

float SampleShadowMap(sampler2D shadowMap, vec2 coords, float compare) {
    return step(compare, texture(shadowMap, coords).r);
}

float SampleShadowMapLinear(sampler2D shadowMap, vec2 coords, float compare, vec2 texelSize) {
    vec2 pixelPos = coords / texelSize + vec2(0.5);
    vec2 fracPart = fract(pixelPos);
    vec2 startTexel = (pixelPos - fracPart) * texelSize;

    float blTexel = SampleShadowMap(shadowMap, startTexel, compare);
    float brTexel = SampleShadowMap(shadowMap, startTexel + vec2(texelSize.x, 0.0), compare);
    float tlTexel = SampleShadowMap(shadowMap, startTexel + vec2(0.0, texelSize.y), compare);
    float trTexel = SampleShadowMap(shadowMap, startTexel + texelSize, compare);

    float mixA = mix(blTexel, tlTexel, fracPart.y);
    float mixB = mix(brTexel, trTexel, fracPart.y);

    return mix(mixA, mixB, fracPart.x);
}

float ApplyPCF(sampler2D shadowMap, vec3 projCoords, float compareDepth) {
    const float NUM_SAMPLES = PCF_NUM_SAMPLES;
    const float SAMPLES_START = (NUM_SAMPLES - 1.0) / 2.0;
    const float NUM_SAMPLES_SQUARED = NUM_SAMPLES * NUM_SAMPLES;

    float lit = 0.0;
    vec2 texelSize = 1.0 / vec2(textureSize(shadowMap, 0));

    for (float y = -SAMPLES_START; y <= SAMPLES_START; y += 1.0) {
        for (float x = -SAMPLES_START; x <= SAMPLES_START; x += 1.0) {
            lit += SampleShadowMapLinear(shadowMap, projCoords.xy + vec2(x, y) * texelSize, compareDepth, texelSize);
        }
    }

    return lit / NUM_SAMPLES_SQUARED;
}

// Light-space position -> shadow-map coordinates in [0, 1] (xy) and depth (z).
vec3 ShadowMapCoords(vec4 fragPosLightSpace) {
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    return projCoords * 0.5 + 0.5;
}
