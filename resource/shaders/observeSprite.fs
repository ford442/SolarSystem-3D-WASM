#version 300 es
#include "common/preamble.glsl"

in vec2 vUv;
flat in vec4 vColor;
flat in vec4 vParams;
in vec3 vPoint;
flat in vec3 vDir;
flat in vec3 vRight;
flat in vec3 vUp;

uniform vec3 sunDir;
uniform float sunTanRadius; // tan of the Sun's angular radius
uniform float sunCoverage;  // fraction of the Sun's disc covered by the Moon, 0..1
uniform mat3 moonBodyFromHorizon; // horizon frame → selenographic axes (X lon 0, Y lon +90 E, Z north)
uniform sampler2D moonTexture;
uniform bool hasMoonTexture;

const float PI = 3.14159265358979;
// Longitude at the centre of the diffuse map. Standard lunar maps (NASA LRO/LROC, Solar System
// Scope) are centred on 0 deg; change this if a deployed texture is not.
const float MOON_MAP_CENTRE_LON = 0.0;

out vec4 fragColor;

// Compared with a half-step margin: the kind is a float attribute, never an exact equality.
const float KIND_SUN = 0.5;
const float KIND_MOON = 1.5;
const float KIND_STAR = 2.5;

void main() {
    // Anything below the horizon is hidden by the ground.
    float aboveHorizon = smoothstep(-0.003, 0.003, normalize(vPoint).y);
    float kind = vParams.x;

    if (kind > KIND_STAR) {
        float profile = exp(-dot(vUv, vUv) * 1.8);
        fragColor = vec4(vColor.rgb, vColor.a * profile * aboveHorizon);
    } else if (kind >= KIND_MOON) {
        vec2 du = vUv / vParams.y;
        float r2 = dot(du, du);
        if (r2 >= 1.0) {
            discard;
        }
        // The surface normal that faces the viewer is -vDir; light comes from the Sun.
        vec3 n = vRight * du.x + vUp * du.y - vDir * sqrt(1.0 - r2);
        float lit = smoothstep(0.0, 0.08, dot(n, sunDir));
        vec3 albedo;
        if (hasMoonTexture) {
            // The surface point's selenographic longitude/latitude → equirectangular map. Using the
            // topocentric normal here means parallax and libration are both already in the frame.
            vec3 nb = moonBodyFromHorizon * n;
            float lon = atan(nb.y, nb.x) - MOON_MAP_CENTRE_LON;
            float lat = asin(clamp(nb.z, -1.0, 1.0));
            // Textures are uploaded unflipped (TextureImage2D loads DDS with flip=false), so t = 0 is the
            // top row of the image = north.
            vec2 uv = vec2(0.5 + lon / (2.0 * PI), 0.5 - lat / PI);
            albedo = texture(moonTexture, uv).rgb * 1.6;
        } else {
            // Faint maria-like mottling until the Moon's texture has loaded.
            float mottle = 0.86 + 0.14 * sin(du.x * 5.0 + 1.3) * sin(du.y * 4.0 - 0.7);
            albedo = vColor.rgb * mottle;
        }
        vec3 color = albedo * (lit + 0.035);
        float edge = 1.0 - smoothstep(0.97, 1.0, sqrt(r2));

        // The dark side hides stars at night, but against a daytime sky it should read as sky,
        // not a hole — except where it covers the Sun, which it must block completely.
        float day = smoothstep(-0.12, 0.20, sunDir.y);
        vec3 toSun = normalize(vPoint) - sunDir;
        float overSun = 1.0 - smoothstep(0.9 * sunTanRadius, 1.1 * sunTanRadius, length(toSun));
        float darkAlpha = mix(1.0, 0.15, day * (1.0 - overSun));
        float alpha = mix(darkAlpha, 1.0, lit);
        fragColor = vec4(color, edge * alpha * aboveHorizon);
    } else if (kind >= KIND_SUN) {
        float r = length(vUv) / vParams.y; // in solar radii
        float disc = 0.0;
        if (r < 1.0) {
            float limb = 1.0 - 0.6 * (1.0 - sqrt(1.0 - r * r));
            disc = limb * 3.0; // saturates to white once added to the sky
        }
        float edgeFade = 1.0 - smoothstep(0.4, 1.0, length(vUv));
        // Glare around the Sun is sunlight scattered in the air and eyes: it dies with the sunlight.
        float halo = 0.6 * exp(-(r - 1.0) * 0.9) * edgeFade * (1.0 - sunCoverage);
        // The corona is only there once the disc is fully hidden: bright pearl-white near the
        // limb, fading over a few solar radii, with fainter equatorial streamers.
        float totality = smoothstep(0.995, 1.0, sunCoverage);
        float angle = atan(vUv.y, vUv.x);
        float streamers = 0.7 + 0.3 * cos(2.0 * angle - 0.6) + 0.15 * cos(6.0 * angle + 1.1);
        float corona = totality * edgeFade * streamers *
                       (0.85 * exp(-(r - 1.0) * 2.4) + 0.28 * exp(-(r - 1.0) * 0.7));
        float intensity = r < 1.0 ? disc : halo + corona;
        vec3 tint = mix(vec3(1.0, 0.95, 0.85), vec3(0.92, 0.95, 1.0), totality);
        fragColor = vec4(tint * intensity, aboveHorizon);
    } else {
        float profile = exp(-dot(vUv, vUv) * 2.5);
        fragColor = vec4(vColor.rgb, vColor.a * profile * aboveHorizon);
    }
}
