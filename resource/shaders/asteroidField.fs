#version 300 es
precision highp float;

in vec3 vFragPos;
in vec3 vNormal;
in vec3 vColor;

uniform vec3 lightPos;
uniform vec3 viewPos;
uniform float ambientFactor;

out vec4 fragColor;

void main() {
    vec3 N = normalize(vNormal);
    vec3 L = normalize(lightPos - vFragPos);
    float ndotl = max(dot(N, L), 0.0);

    // Soft rim so rocks stay readable against dark space.
    vec3 V = normalize(viewPos - vFragPos);
    float rim = pow(1.0 - max(dot(N, V), 0.0), 3.0) * 0.15;

    vec3 lit = vColor * (ambientFactor + ndotl * 0.95 + rim);
    fragColor = vec4(lit, 1.0);
}
