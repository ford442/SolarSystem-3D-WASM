#version 300 es
precision highp float;

in vec2 TexCoords;

layout(binding = TU_DIFFUSE) uniform sampler2D uImage;
uniform float uIntensity;

out vec4 fragColor;

void main() {
    vec3 glow = texture(uImage, TexCoords).rgb * uIntensity;
    fragColor = vec4(glow, 1.0);
}
