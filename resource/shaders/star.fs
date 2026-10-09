#version 300 es

#include "common/noise_simplex3d.glsl"

in vec3 fPosition;
in vec3 fWorldPosition;
in vec3 fNormal;
uniform vec3 viewPos;      // this eye's position (world)
uniform vec3 shiftStarColor;
uniform vec3 colorMult;
uniform sampler2D colorMap;
uniform float uColorMap;
uniform float starRadiusInKilometers;
uniform bool isVisible;

out vec4 fragColor;

// Limb darkening, power law I(mu)/I(1) = mu^alpha (Hestroffer & Magnan 1998), with alpha
// fitted per channel to the solar continuum at ~680/550/440 nm: the limb is darker and
// redder because the line of sight there ends in cooler, higher photosphere.
const vec3 LIMB_DARKENING_ALPHA = vec3(0.397, 0.503, 0.652);

void main() {
    vec3 position = fPosition;
    vec3 sPosition = position * starRadiusInKilometers;

    // Sunspots
    float s = 0.3;
    float frequency = 0.00001;
    float t1 = snoise(sPosition * frequency) - s;
    float t2 = snoise((sPosition + starRadiusInKilometers) * frequency) - s;
    float ss = (max(t1, 0.0) * max(t2, 0.0)) * 2.0;

    // Granulation
    float n = (noise(position, 4, 40.0, 0.7) + 1.0) * 0.5;
    float totalNoise = n - ss;

    vec3 colorByTemperature = texture(colorMap, vec2(uColorMap, 0.0)).rgb;
    vec3 surface = colorByTemperature * (shiftStarColor * colorMult + totalNoise - 0.1);

    // mu = cosine between the surface normal and the line of sight.
    float mu = clamp(dot(normalize(fNormal), normalize(viewPos - fWorldPosition)), 0.0, 1.0);
    vec3 limb = pow(vec3(max(mu, 1e-4)), LIMB_DARKENING_ALPHA);

    fragColor = vec4(surface * limb, float(isVisible));
}
