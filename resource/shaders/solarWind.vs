#version 300 es
// webgpu: replaced - point sprites (gl_PointSize/gl_PointCoord); WebGPU draws the wind as instanced quads from a compute pass (docs/plans/WEBGPU_BACKEND_PLAN.md)

// Draws the solar-wind particles solarWindUpdate.vs advected (render/SolarWindRenderer):
// additive points, depth-tested, sized in pixels. Colour says what a particle is doing —
// warm slow wind, blue-white fast streams, white CME sheath, and the body's auroral colour
// once it is on a field line, flashing where it lands on the oval.

layout (location = 0) in vec4 aState0;
layout (location = 1) in vec4 aState1;

out vec4 vColor;

uniform int uGroup;

#include "common/frame.glsl"
#include "common/log_depth.glsl"
#include "common/space_weather.glsl"

const vec3 kSlowWind = vec3(1.0, 0.72, 0.42);
const vec3 kFastWind = vec3(0.55, 0.8, 1.0);
const vec3 kCmeSheath = vec3(1.0, 0.92, 1.0);

void Hide() {
    vColor = vec4(0.0);
    gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
    gl_PointSize = 1.0;
}

void main() {
    if (aState1.x < 0.0) {
        Hide();
        return;
    }
    vec3 renderPosition;
    vec3 color;
    float alpha;
    float size = sw.look.x;

    if (uGroup < 0) {
        vec3 p = aState0.xyz;
        float r = max(length(p), 1e-6);
        vec3 dir = p / r;
        vec3 scene = dir * SwAuToScene(r);
        renderPosition = scene + sw.sun.xyz;
        float spawn = sw.step.z;
        alpha = smoothstep(spawn, spawn * 3.0, r) * (1.0 - smoothstep(aState1.w * 0.75, aState1.w, r)) *
                (1.0 - smoothstep(sw.look.z * 0.5, sw.look.z, r));
        color = mix(kSlowWind, kFastWind, aState1.z);
        float cme = 0.0;
        for (int i = 0; i < 4; ++i) {
            if (i >= sw.counts.y) {
                break;
            }
            cme = max(cme, SwCmeSample(i, dir, r).y * sw.cmes[i].shape.w);
        }
        color = mix(color, kCmeSheath, clamp(cme, 0.0, 1.0));
        alpha *= (0.35 + 0.25 * aState1.z) * (1.0 + 3.0 * cme);
        size *= 1.0 + 0.8 * clamp(cme, 0.0, 1.0);
        // Where a body's own group draws the wind, the background steps aside; nothing is
        // drawn inside a magnetopause.
        for (int i = 0; i < 6; ++i) {
            if (i >= sw.counts.x) {
                break;
            }
            SwBody b = sw.bodies[i];
            if (b.helio.w <= 0.0) {
                continue; // a slot whose body is not in the scene
            }
            vec3 q = (scene - b.helio.xyz) / (b.helio.w * max(b.axis.w, 1.0));
            float d = length(q);
            alpha *= b.render.w > 0.5 ? smoothstep(3.0, 6.0, d) : smoothstep(1.0, 1.4, d);
        }
    } else {
        SwBody b = sw.bodies[uGroup];
        vec3 q = aState0.xyz;
        renderPosition = b.render.xyz + q * b.helio.w;
        float s = b.axis.w;
        float storm = b.state.w;
        float density = clamp(b.colorEdge.a, 1.0, 5.0);
        if (aState1.x > 0.5) {
            float progress = aState0.w;
            vec3 aurora = b.colorLow.rgb;
            // Wind-coloured on the way down; the auroral colour only as it reaches the oval.
            color = mix(mix(kSlowWind, kFastWind, 0.5), aurora, smoothstep(0.6, 0.95, progress));
            float flash = smoothstep(0.92, 1.0, progress) * (1.0 - smoothstep(1.05, 1.18, progress));
            alpha = (0.5 + 0.5 * smoothstep(0.2, 0.9, progress)) * (1.0 + 5.0 * flash);
            size *= 1.0 + 2.0 * flash;
        } else {
            float along = dot(q, b.sunward.xyz);
            float r = length(q);
            float cosTheta = along / max(r, 1e-4);
            float bowShock = 1.3 * SwMagnetopause(s, b.sunward.w + 0.12, cosTheta);
            float sheath = 1.0 - smoothstep(bowShock * 0.95, bowShock * 1.1, r);
            float lateral = length(q - along * b.sunward.xyz);
            alpha = smoothstep(6.0 * s, 4.5 * s, along) * smoothstep(-20.0 * s, -13.0 * s, along) *
                    (1.0 - smoothstep(4.5 * s, 7.0 * s, lateral));
            alpha *= 0.3 * (1.0 + 1.2 * sheath);
            color = mix(mix(kSlowWind, kFastWind, clamp(b.state.z - 1.0, 0.0, 1.0)), kCmeSheath, storm);
        }
        alpha *= sqrt(density) * (1.0 + 1.5 * storm);
    }

    // Most of a group's particles are outside the part of it that shows (the faded tail, the
    // far flanks, a parcel spawning): clip them instead of rasterising a zero.
    if (alpha * sw.look.y < 0.004) {
        Hide();
        return;
    }
    vColor = vec4(color * sw.look.y, clamp(alpha, 0.0, 4.0));
    gl_Position = ApplyLogDepth(frame.viewProj * vec4(renderPosition, 1.0));
    gl_PointSize = clamp(size, 1.0, 16.0);
}
