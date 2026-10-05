#version 300 es
#include "common/noise_simplex3d.glsl"

in vec2 fPosition;
in vec3 ringTint;

uniform vec3 colorMult;
uniform sampler2D colorMap;
uniform float noiseZ;
uniform float uColorMap; // 'u' (same 'x') texture coordinate

out vec4 fragColor;

void main() {
    const float spikeFrequency = 15.5;
    const float spikeShift = 0.2;
    const float spikeMult2 = 0.02;

    vec2 fTex = (vec2(fPosition.x, fPosition.y) + 1.0) / 2.0;
    vec2 nDistVec = normalize(vec2(fPosition.x, fPosition.y));
    float spikeNoise = snoise(vec3(nDistVec, noiseZ) * spikeFrequency);
    float spikeVal = spikeNoise + spikeShift;

    float dist = length(fPosition);

    float brightness = ((1.0 / pow(dist + 0.15, 0.5)) - 1.0);
    brightness = max(brightness, 0.0) * 0.7;
    float spikeBrightness = brightness * spikeMult2 * clamp(spikeVal, 0.0, 1.0) * 0.35;

    float ovCol = (pow(1.0 - dist, 2.5) * (dist) * 3.0) * (uColorMap + spikeNoise * spikeMult2);
    ovCol = max(ovCol, 0.0);
    float centerGlow = 1.0 / pow(dist + 0.96, 40.0) * 0.1;

    vec3 temperatureColor = texture(colorMap, vec2(uColorMap, 0.0)).rgb;
    vec3 color = temperatureColor * colorMult;

    vec2 ap = abs(vec2(fPosition.x, fPosition.y));

    float hRay = (1.0 - (1.0 / (1.0 + exp(-((ap.y * 35.0 + 0.2) * 4.0 * 3.1415926) + 2.0 * 3.1415926))) - max(ap.x - 0.1, 0.0));
    hRay = max(hRay * 0.2, 0.0) * 0.35;

    fragColor = vec4(color * (brightness + centerGlow + spikeBrightness + hRay + ovCol), 1.0);

    fragColor.rgb *= fragColor.rgb; // Reverse the gamma [Обратная гамма]
    fragColor.rgb *= fragColor.rgb; // Reverse the gamma [Обратная гамма]

    if (ringTint != vec3(0.0))
        fragColor.rgb *= ringTint;
}

