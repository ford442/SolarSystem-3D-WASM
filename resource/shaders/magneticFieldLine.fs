#version 300 es
precision mediump float;

in float vArcLength;
in float vEdgeFade;

uniform vec3 lineColor;
uniform float uFlowSpeed;
uniform float uTime;
uniform float uBaseOpacity;

out vec4 fragColor;

void main() {
    float pulse = 0.5 + 0.5 * sin(vArcLength * 40.0 - uTime * uFlowSpeed);
    float edge = smoothstep(0.0, 0.55, vEdgeFade);
    float alpha = uBaseOpacity * pulse * edge;
    fragColor = vec4(lineColor, alpha);
}
