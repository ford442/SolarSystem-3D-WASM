#version 300 es

// A soft round point, added onto the scene (solarWind.vs).

in vec4 vColor;

out vec4 fragColor;

void main() {
    vec2 c = gl_PointCoord * 2.0 - 1.0;
    float falloff = exp(-3.0 * dot(c, c));
    if (falloff < 0.02) {
        discard;
    }
    fragColor = vec4(vColor.rgb * vColor.a * falloff, 1.0);
}
