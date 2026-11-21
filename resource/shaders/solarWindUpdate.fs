#version 300 es

// The update pass runs with GL_RASTERIZER_DISCARD, so nothing reaches this stage; GLSL ES
// still needs a fragment shader to link the program (solarWindUpdate.vs).

out vec4 fragColor;

void main() {
    fragColor = vec4(0.0);
}
