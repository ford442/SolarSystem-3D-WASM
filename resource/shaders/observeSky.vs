#version 300 es

// Fullscreen triangle straight from gl_VertexID; the fragment stage rebuilds the view ray.
out vec2 vNdc;

void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    vNdc = p * 2.0 - 1.0;
    gl_Position = vec4(vNdc, 0.0, 1.0);
}
