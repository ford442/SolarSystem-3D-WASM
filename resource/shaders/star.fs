#version 300 es
#include "common/noise_simplex3d.glsl"

in vec3 fPosition;
uniform vec3 centerDir;
uniform vec3 shiftStarColor;
uniform vec3 colorMult;
uniform sampler2D colorMap;
uniform float uColorMap;
uniform float starRadiusInKilometers;
uniform bool isVisible;

out vec4 fragColor;

void main() {
    vec3 position = fPosition;
    vec3 sPosition = position * starRadiusInKilometers;

    // Sunspots
    float s = 0.3;
    float frequency = 0.00001;
    float t1 = snoise(sPosition * frequency) - s;
    float t2 = snoise((sPosition + starRadiusInKilometers) * frequency) - s;
    float ss = (max(t1, 0.0) * max(t2, 0.0)) * 2.0;

    float n = (noise(position, 4, 40.0, 0.7) + 1.0) * 0.5;
    float totalNoise = n - ss;

    vec3 colorByTemperature = texture(colorMap, vec2(uColorMap, 0.0)).rgb;

    float theta = 1.0 - dot(centerDir, fPosition) * 0.7;

    fragColor = vec4(colorByTemperature * (shiftStarColor * colorMult + totalNoise - 0.5 - theta), float(isVisible));
}
