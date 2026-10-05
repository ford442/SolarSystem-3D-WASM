#version 300 es
#include "common/noise_simplex4d.glsl"

uniform vec3 starShiftColor;
uniform float deltaTime;
uniform float maxSize;

in vec3 fPosition;
out vec4 fragColor;

void main() {
    /* Edit these */
    const float brightnessMultiplier = 0.9;   // The higher the number, the brighter the corona will be.
    const float smootheningMultiplier = 0.15; // How smooth the irregular effect is, the higher the smoother.
    const float ringIntesityMultiplier = 2.8; // The higher the number, the smaller the ring.
    const float coronaSizeMultiplier = 2.35;     // The higher the number, the smaller the corona.
    const float frequency = 1.8;              // The frequency of the irregularities.
    const float fDetail = 0.7;                // The higher the number, the more detail the corona will have. (Might be more GPU intensive when higher, 0.7 seems fine for the normal PC)
    const int iDetail = 10;                   // The higher the number, the more detail the corona will have.
    const float irregularityMultiplier = 4.0;   // The higher the number, the more irregularities and bigger ones. (Might be more GPU intensive when higher, 4 seems fine for the normal PC)

    /* Don't edit these */
    float t = deltaTime * 10.0 - length(fPosition);

    // Offset normal with noise
    float ox = snoise(vec4(fPosition, t) * frequency);
    float oy = snoise(vec4((fPosition + (1000.0 * irregularityMultiplier)), t) * frequency);
    float oz = snoise(vec4((fPosition + (2000.0 * irregularityMultiplier)), t) * frequency);
    float om = snoise(vec4((fPosition + (4000.0 * irregularityMultiplier)), t) * frequency) * snoise(vec4((fPosition + (250.0 * irregularityMultiplier)), t) * frequency);
    vec3 offsetVec = vec3(ox * om, oy * om, oz * om) * smootheningMultiplier;

    // Get the distance vector from the center
    vec3 nDistVec = normalize(fPosition + offsetVec);

    // Get noise with normalized position to offset the original position
    vec3 position = fPosition + noise(vec4(nDistVec, t), iDetail, 1.5, fDetail) * smootheningMultiplier;

    // Calculate brightness based on distance
    float dist = length(position + offsetVec) * coronaSizeMultiplier;
    float brightness = (1.0 / (dist * dist) - 0.1) * (brightnessMultiplier - 0.4);
    float brightness2 = (1.0 / (dist * dist)) * brightnessMultiplier;

    // Calculate color
    vec3 color = starShiftColor * brightness;

    fragColor = vec4(color, clamp(brightness, 0.0, 1.0) * (cos(clamp(brightness, 0.0, 0.5)) / (cos(clamp(brightness2 / ringIntesityMultiplier, 0.0, 1.5)) * 2.0)));
    fragColor.rgb *= fragColor.rgb; // Reverse the gamma [Обратная гамма]
    fragColor.rgb *= fragColor.rgb; // Reverse the gamma [Обратная гамма]
}