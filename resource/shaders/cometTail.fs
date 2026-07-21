#version 300 es
precision mediump float;

in vec2 vUV;
in float vFade;

uniform vec3 tailColor;
uniform float tailOpacity;

out vec4 fragColor;

void main() {
    // Soft lateral falloff + stronger tip fade.
    float lateral = 1.0 - abs(vUV.x * 2.0 - 1.0);
    lateral = smoothstep(0.0, 0.85, lateral);
    float along = vFade * vFade;
    float alpha = lateral * along * tailOpacity;
    fragColor = vec4(tailColor * alpha, alpha);
}
