#version 300 es
precision mediump float;

in float vArcLength;
in float vSide;

uniform vec3 lineColor;
uniform float flowSpeed;
uniform float uTime;
uniform float uOpacity;

out vec4 fragColor;

void main() {
    float pulse = 0.5 + 0.5 * sin(vArcLength * 18.0 - uTime * flowSpeed);
    float edge = 1.0 - abs(vSide);
    float lateral = smoothstep(0.0, 0.7, edge);
    float alpha = lateral * (0.22 + 0.78 * pulse) * uOpacity;
    fragColor = vec4(lineColor * alpha, alpha);
}
