#version 300 es
precision highp float;
precision highp int;

in vec2 TexCoords;

layout(binding = TU_DIFFUSE) uniform sampler2D hdrBuffer;
uniform bool hdr;
uniform float gamma;
uniform float exposure;
// 1 / size of hdrBuffer in pixels: the FXAA taps are offsets of this.
uniform vec2 texelSize;

out vec4 fragColor;

// HDR texel to the display value the default framebuffer receives.
vec3 Display(vec2 uv) {
    vec3 hdrColor = texture(hdrBuffer, uv).rgb;
    if (hdr) {
        vec3 result = 1.0 - exp(-hdrColor * exposure);

        // Also gamma correct while we're at it
        return pow(result, vec3(gamma));
    }
    return pow(hdrColor, vec3(1.0 / gamma));
}

float Luma(vec3 rgb) {
    return dot(rgb, vec3(0.299, 0.587, 0.114));
}

// The tone map is the antialiasing pass. The colour pass draws into a single-sampled HDR
// texture, so a multisampled default framebuffer would only resolve an image that is already
// resolved (web/src/webglContext.ts and PlatformWindow.cpp therefore ask for no context
// samples when HDR is on). This is Lottes' FXAA 2 "console" filter, run on the tone-mapped
// value: edges are found on what the screen shows, and an HDR sun disc many times brighter
// than its neighbours would otherwise decide every contrast test.
#define FXAA_REDUCE_MIN (1.0 / 128.0)
#define FXAA_REDUCE_MUL (1.0 / 8.0)
#define FXAA_SPAN_MAX 8.0

vec3 Fxaa(vec2 uv) {
    vec3 rgbNW = Display(uv + vec2(-1.0, -1.0) * texelSize);
    vec3 rgbNE = Display(uv + vec2(1.0, -1.0) * texelSize);
    vec3 rgbSW = Display(uv + vec2(-1.0, 1.0) * texelSize);
    vec3 rgbSE = Display(uv + vec2(1.0, 1.0) * texelSize);
    vec3 rgbM = Display(uv);

    float lumaNW = Luma(rgbNW);
    float lumaNE = Luma(rgbNE);
    float lumaSW = Luma(rgbSW);
    float lumaSE = Luma(rgbSE);
    float lumaM = Luma(rgbM);
    float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));

    // Edge direction from the corner lumas; the texture's v axis runs up, hence the signs.
    vec2 dir;
    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    dir.y = (lumaNW + lumaSW) - (lumaNE + lumaSE);

    float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * (0.25 * FXAA_REDUCE_MUL), FXAA_REDUCE_MIN);
    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
    dir = clamp(dir * rcpDirMin, vec2(-FXAA_SPAN_MAX), vec2(FXAA_SPAN_MAX)) * texelSize;

    vec3 rgbA = 0.5 * (Display(uv + dir * (1.0 / 3.0 - 0.5)) + Display(uv + dir * (2.0 / 3.0 - 0.5)));
    vec3 rgbB = rgbA * 0.5 + 0.25 * (Display(uv + dir * -0.5) + Display(uv + dir * 0.5));
    float lumaB = Luma(rgbB);
    // A wide blend that strays outside the local range crossed into a different feature.
    return (lumaB < lumaMin || lumaB > lumaMax) ? rgbA : rgbB;
}

void main() {
    fragColor = vec4(Fxaa(TexCoords), 1.0);
}
