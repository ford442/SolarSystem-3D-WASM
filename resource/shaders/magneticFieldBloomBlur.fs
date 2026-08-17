#version 300 es
precision highp float;

in vec2 TexCoords;

uniform sampler2D uImage;
uniform vec2 uDirection;

out vec4 fragColor;

void main() {
    // 9-tap Gaussian; uDirection is one texel in the blur axis.
    const float w0 = 0.2270270270;
    const float w1 = 0.1945945946;
    const float w2 = 0.1216216216;
    const float w3 = 0.0540540541;
    const float w4 = 0.0162162162;

    vec4 result = texture(uImage, TexCoords) * w0;
    result += texture(uImage, TexCoords + uDirection) * w1;
    result += texture(uImage, TexCoords - uDirection) * w1;
    result += texture(uImage, TexCoords + uDirection * 2.0) * w2;
    result += texture(uImage, TexCoords - uDirection * 2.0) * w2;
    result += texture(uImage, TexCoords + uDirection * 3.0) * w3;
    result += texture(uImage, TexCoords - uDirection * 3.0) * w3;
    result += texture(uImage, TexCoords + uDirection * 4.0) * w4;
    result += texture(uImage, TexCoords - uDirection * 4.0) * w4;
    fragColor = result;
}
