#version 300 es
precision highp float;
precision highp int;

in vec3 TexCoords;
uniform samplerCube skybox;

out vec4 fragColor;

void main() {
    fragColor = texture(skybox, TexCoords);
}