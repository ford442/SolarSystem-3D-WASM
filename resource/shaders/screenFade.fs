#version 300 es
// The jump between star systems (Application::UpdateSystemJump): the frame washes into the
// departing star's colour, goes black while the scene is swapped, and fades back in.

uniform vec4 fadeColor; // rgb: colour; a: how much of the frame it covers

out vec4 fragColor;

void main() {
    fragColor = fadeColor;
}
