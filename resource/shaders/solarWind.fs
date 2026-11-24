#version 300 es
// webgpu: replaced - point sprites (gl_PointSize/gl_PointCoord); WebGPU draws the wind as instanced quads from a compute pass (docs/plans/WEBGPU_BACKEND_PLAN.md)

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
