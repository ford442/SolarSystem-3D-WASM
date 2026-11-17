// render.procedural (PROCEDURAL_SURFACE): a planet with no texture, drawn from noise on its
// sphere direction. Exoplanets have no maps, so the generator picks a mode and three colours
// from the planet's equilibrium temperature and radius (scripts/generate-exoplanet-systems.mjs)
// and CatalogMaterial::BindMaterial uploads them. It is an illustration of a type, not of
// that world: the UI says so.
//
// The direction comes from the sphere mesh's UV convention (as terrain.fs uses it): u = 0.5 on
// +X rising east, longitude -atan2(z, x); v = 0 at the +Y pole. Noise on the direction has no
// seam at u = 0/1 and no pinch at the poles.
#include "common/noise_simplex3d.glsl"

uniform int procMode;        // 0 rocky, 1 banded gas, 2 lava, 3 ice
uniform float procSeed;      // offsets the noise so planets of one style differ
uniform vec3 procColor0;     // rocky: ocean / lowland; gas: belts; lava: crust; ice: dark ice
uniform vec3 procColor1;     // rocky: land; gas: zones; lava: cooled flows; ice: ice
uniform vec3 procColor2;     // rocky: highland / cap; gas: highlights; lava: melt; ice: frost
uniform float procBands;     // gas: zonal band count
uniform float procSeaLevel;  // rocky: about the ocean fraction (sea where noise < seaLevel - 0.5); 0 = none
uniform float procPolarCap;  // |sin latitude| above which procColor2 caps the poles; 0 none
uniform float procEmissive;  // self-lit strength of the hot colour (lava, hot giants)

vec3 ProceduralDirection(vec2 uv) {
    const float PI = 3.14159265358979;
    float lon = (uv.x - 0.5) * 2.0 * PI;
    float lat = (0.5 - uv.y) * PI;
    float c = cos(lat);
    return vec3(c * cos(lon), sin(lat), -c * sin(lon));
}

vec3 ProceduralOffset() {
    return vec3(procSeed * 17.31, procSeed * 5.97, procSeed * 11.13);
}

// Albedo (rgb) and emission weight (a) at `uv`.
vec4 ProceduralAlbedo(vec2 uv) {
    vec3 dir = ProceduralDirection(uv);
    vec3 p = dir + ProceduralOffset();
    float lat = dir.y;

    if (procMode == 1) {
        // Banded giant: zonal stripes by latitude, sheared by low-frequency turbulence.
        float warp = noise(p * vec3(1.0, 4.0, 1.0), 4, 1.6, 0.55);
        float band = sin((lat + 0.08 * warp) * procBands * 3.14159265);
        float fine = noise(p * vec3(3.0, 18.0, 3.0), 3, 1.0, 0.5);
        vec3 color = mix(procColor0, procColor1, smoothstep(-0.4, 0.4, band + 0.25 * fine));
        float storm = smoothstep(0.72, 0.9, noise(p * 2.3, 3, 1.0, 0.5));
        color = mix(color, procColor2, storm * 0.6);
        return vec4(color, procEmissive * 0.3);
    }

    float h = noise(p, 6, 1.4, 0.55); // about -1..1

    if (procMode == 2) {
        // Lava world: dark crust broken by glowing cracks.
        float cracks = 1.0 - absNoise(p * 1.7, 5, 1.0, 0.6);
        float melt = smoothstep(0.72, 0.92, cracks) * (0.6 + 0.4 * smoothstep(-0.2, 0.4, h));
        vec3 crust = mix(procColor0, procColor1, smoothstep(-0.5, 0.5, h));
        return vec4(mix(crust, procColor2, melt), melt * procEmissive);
    }

    if (procMode == 3) {
        // Ice world: frost plains with darker ridged lineae.
        float ridges = ridgedNoise(p * 1.3, 5, 1.0, 0.5);
        vec3 color = mix(procColor1, procColor2, smoothstep(-0.3, 0.6, h));
        color = mix(color, procColor0, smoothstep(0.55, 0.85, ridges) * 0.7);
        return vec4(color, 0.0);
    }

    // Rocky: sea below seaLevel, land and highlands above, polar caps.
    vec3 color;
    if (procSeaLevel > 0.0 && h < procSeaLevel - 0.5) {
        color = procColor0 * (0.75 + 0.25 * smoothstep(-1.0, procSeaLevel - 0.5, h));
    } else {
        float land = smoothstep(-0.6, 0.7, h);
        color = mix(procSeaLevel > 0.0 ? procColor1 : procColor0, procColor1, land);
        color = mix(color, procColor2, smoothstep(0.35, 0.8, h));
    }
    if (procPolarCap > 0.0) {
        float cap = smoothstep(procPolarCap, procPolarCap + 0.06, abs(lat) + 0.05 * h);
        color = mix(color, vec3(0.92, 0.94, 0.97), cap);
    }
    return vec4(color, procEmissive * smoothstep(0.4, 0.9, h));
}
