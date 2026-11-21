// Space weather shared by the solar-wind particles, the aurora shells and the field lines
// (docs/ARCHITECTURE.md §13). The block mirrors SpaceWeatherUniformData in
// src/gfx/SpaceWeatherUniforms.h (std140, binding 1; Shader binds it at link). The numbers
// in it come from src/astro/SpaceWeather.cpp: shaders draw the model, they do not run it.

struct SwBody {
    vec4 helio;     // xyz heliocentric scene position, w drawn radius (scene units)
    vec4 render;    // xyz render-frame centre, w 1 when its particle group runs this frame
    vec4 axis;      // xyz magnetic north (unit), w standoff (drawn radii)
    vec4 sunward;   // xyz unit vector to the Sun, w Shue flaring exponent
    vec4 state;     // x Kp / 9, y standoff / quiet standoff, z wind speed / 400 km/s, w CME strength
    vec4 oval;      // magnetic latitude (rad): equatorward midnight, noon; poleward midnight, noon
    vec4 colorLow;  // rgb lower curtain, a brightness
    vec4 colorHigh; // rgb upper glow, a quiet standoff (drawn radii)
    vec4 colorEdge; // rgb storm border, a wind density / quiet density
    vec4 shell;     // x bottom / radius, y top / radius, z Kp, w aurora mode
};

struct SwCme {
    vec4 axis;  // xyz direction, w front distance (AU)
    vec4 shape; // x cos(flank edge), y cos(core), z front speed / 400 km/s, w brightness (0: unused)
};

layout(std140) uniform MagnetosphereUBO {
    vec4 sun;   // xyz the Sun, render frame; w its drawn radius
    vec4 wind;  // x AU per visual second at 400 km/s, y outer edge (AU), z Sun rotation (rad per visual s), w visual clock
    vec4 step;  // x dt (visual s), y fast-wind belt half-width (rad), z spawn radius (AU), w cycle phase
    vec4 look;  // x point size (px), y brightness, z far fade (AU), w frame counter
    ivec4 counts; // x bodies, y CMEs, z knots
    vec4 knotAu[4];
    vec4 knotScene[4];
    SwBody bodies[6];
    SwCme cmes[4];
} sw;

// Kept in step with SpaceWeather::kShellThickness.
const float kSwCmeShellThickness = 0.45;

float SwKnotAu(int i) {
    return sw.knotAu[i / 4][i - (i / 4) * 4];
}

float SwKnotScene(int i) {
    return sw.knotScene[i / 4][i - (i / 4) * 4];
}

// OrbitLayout::AuToSceneDistance: piecewise linear through the knots, proportional outside.
float SwAuToScene(float au) {
    int n = sw.counts.z;
    float a0 = SwKnotAu(0);
    float s0 = SwKnotScene(0);
    if (au <= a0) {
        return s0 * au / a0;
    }
    for (int i = 1; i < 16; ++i) {
        if (i >= n) {
            break;
        }
        float a1 = SwKnotAu(i);
        float s1 = SwKnotScene(i);
        if (au <= a1) {
            return mix(s0, s1, (au - a0) / (a1 - a0));
        }
        a0 = a1;
        s0 = s1;
    }
    return s0 * au / a0;
}

// A body's magnetic frame: z the magnetic north pole, x toward magnetic noon (the Sun, made
// perpendicular to the pole), y = z × x toward dusk.
void SwMagneticFrame(SwBody b, out vec3 x, out vec3 y, out vec3 z) {
    z = b.axis.xyz;
    vec3 noon = b.sunward.xyz - dot(b.sunward.xyz, z) * z;
    float len = length(noon);
    x = len > 1e-4 ? noon / len : normalize(cross(z, vec3(0.0, 0.0, 1.0)) + vec3(1e-4, 0.0, 0.0));
    y = cross(z, x);
}

// Shue et al. (1997): r(θ) = r0 (2 / (1 + cos θ))^α. The tail is a cylinder past ~160°.
float SwMagnetopause(float standoff, float alpha, float cosTheta) {
    return standoff * pow(2.0 / max(1.0 + cosTheta, 0.06), alpha);
}

// SpaceWeather's CME shell at (unit direction, AU): x field/speed strength, y sheath density.
vec2 SwCmeSample(int i, vec3 dir, float rAu) {
    SwCme c = sw.cmes[i];
    float front = c.axis.w;
    if (c.shape.w <= 0.0 || rAu > front) {
        return vec2(0.0);
    }
    float inner = front * (1.0 - kSwCmeShellThickness);
    if (rAu < inner) {
        return vec2(0.0);
    }
    float angular = smoothstep(c.shape.x, c.shape.y, dot(dir, c.axis.xyz));
    float x = (front - rAu) / max(front - inner, 1e-6);
    float rise = smoothstep(0.0, 0.04, x);
    float bulk = 0.75 + 0.25 * exp(-pow((x - 0.15) / 0.2, 2.0));
    float tail = 1.0 - smoothstep(0.65, 1.0, x);
    return vec2(angular * rise * bulk * tail, angular * rise * exp(-x / 0.15));
}
