#version 300 es
precision mediump float;

in float vArcLength;
in float vEdgeFade;

uniform vec3 lineColor;
uniform float uBaseOpacity;
uniform int uFocused;

out vec4 fragColor;

void main() {
    float dash = 0.55 + 0.45 * sin(vArcLength * 0.08);
    float edge = smoothstep(0.0, 0.55, vEdgeFade);
    float glow = uFocused != 0 ? 1.35 : 1.0;
    float alpha = uBaseOpacity * dash * edge * glow;
    fragColor = vec4(lineColor * glow, alpha);
}
