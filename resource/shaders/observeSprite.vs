#version 300 es

// One instanced quad per Sun/Moon/planet/star. The Sun and Moon are discs laid on the tangent
// plane at their direction; planets and stars are fixed-size screen-space points.
layout (location = 0) in vec2 aCorner;
layout (location = 1) in vec4 iPosSize;  // direction xyz, half-extent (tan units for discs, pixels for points)
layout (location = 2) in vec4 iColor;
layout (location = 3) in vec4 iParams;   // x = kind, y = disc radius / extent

uniform mat3 viewRot;
uniform mat4 projection;
uniform mat3 starRotation;  // equatorial J2000 → horizon frame, applied to stars only
uniform float limitMag;     // faintest star that stands out against the current sky
uniform vec2 viewportPx;    // render target size in pixels

const float KIND_SUN = 0.5;
const float KIND_STAR = 2.5;

out vec2 vUv;
flat out vec4 vColor;
flat out vec4 vParams;
out vec3 vPoint;   // tangent-plane point, normalised in the fragment stage
flat out vec3 vDir;
flat out vec3 vRight;
flat out vec3 vUp;

void main() {
    float kind = iParams.x;
    bool isStar = kind > KIND_STAR;
    // Stars and planets are points: a fixed size in pixels, so they stay round out to the edge
    // of a wide field of view. The Sun and Moon are true angular discs on the tangent plane.
    bool isPoint = isStar || kind < KIND_SUN;

    vec3 d = normalize(iPosSize.xyz);
    float halfExtent = iPosSize.w;   // pixels for points, tan-plane units for discs
    vec4 color = iColor;
    bool culled = false;
    if (isStar) {
        // Stars are static J2000 vectors; their size and brightness follow the sky glow, so
        // they fade in through twilight and are gone by day.
        d = normalize(starRotation * d);
        float margin = limitMag - iParams.y;
        culled = margin < -0.2;
        halfExtent = 1.1 + 0.3 * clamp(margin, 0.0, 9.0);
        color.a = clamp(0.35 + 0.1 * margin, 0.0, 1.0);
    }

    vec3 helper = abs(d.y) > 0.99 ? vec3(0.0, 0.0, -1.0) : vec3(0.0, 1.0, 0.0);
    vec3 right = normalize(cross(d, helper));
    vec3 up = cross(right, d);

    vUv = aCorner;
    vColor = color;
    vParams = iParams;
    vDir = d;
    vRight = right;
    vUp = up;

    if (isPoint) {
        vPoint = d;
        vec4 clip = projection * vec4(viewRot * d, 1.0);
        clip.xy += aCorner * halfExtent * (2.0 / viewportPx) * clip.w;
        gl_Position = culled ? vec4(2.0, 2.0, 2.0, 1.0) : clip;
    } else {
        vec3 p = d + (right * aCorner.x + up * aCorner.y) * halfExtent;
        vPoint = p;
        gl_Position = projection * vec4(viewRot * p, 1.0);
    }
}
