#version 300 es
uniform sampler2D lensTexture;
uniform vec3 color;

uniform bool isPlanetaryRingInView;

uniform vec3 cameraPosition;
uniform vec3 ringCenter; // Center of disk in eye space
uniform vec3 ringNormal; // Disk plane normal in eye space
uniform vec2 ringInnerOuterRadiuses; // x = Inner, y = Outer
uniform sampler2D ringDiffuse;

in vec3 fCenter;
in vec2 fPosition;
in vec2 fUV;
in float fIntensity;

out vec4 fragColor;

#include "common/raytrace.glsl"
#include "common/ring.glsl"

void main() {
    float intensity = fIntensity;

    if (isPlanetaryRingInView) {
        float u;
        if (RingCrossing(cameraPosition, normalize(fCenter - cameraPosition), fCenter - ringCenter,
                         ringCenter, ringNormal, ringInnerOuterRadiuses, u)) {
            vec4 ringColor = texture(ringDiffuse, vec2(u, 0.0));
            intensity *= 1.0 - ringColor.a * 0.65;
        }
    }

    vec4 texColor = texture(lensTexture, fUV) * intensity;
    fragColor = vec4(texColor.rgb * color, texColor.a);
}