#version 300 es
// One full-screen triangle from gl_VertexID (no vertex buffer): Renderer::RenderScreenFade.

void main() {
    vec2 corner = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);
}
