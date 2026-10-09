// Analytic ray tests shared by the lighting, atmosphere, flare and glow shaders.
// https://www.scratchapixel.com/lessons/3d-basic-rendering/minimal-ray-tracer-rendering-simple-shapes/

void swapFloats(inout float left, inout float right) {
    float temp = left;
    left = right;
    right = temp;
}

// Real roots of a*x^2 + b*x + c, ordered x0 <= x1. Uses the cancellation-safe form.
bool solveQuadratic(float a, float b, float c, out float x0, out float x1) {
    x0 = 0.0;
    x1 = 0.0;
    float discr = b * b - 4.0 * a * c;

    if (discr < 0.0)
        return false;

    if (discr == 0.0) {
        x0 = x1 = -0.5 * b / a;
    } else {
        float q = (b > 0.0) ? -0.5 * (b + sqrt(discr)) : -0.5 * (b - sqrt(discr));
        x0 = q / a;
        x1 = c / q;
    }

    if (x0 > x1)
        swapFloats(x0, x1);

    return true;
}

// True when the ray from `origin` along `dir` hits the sphere in front of the origin.
bool intersectSphereAhead(vec3 origin, vec3 dir, vec3 center, float radiusSquared) {
    float t0, t1;
    vec3 L = origin - center;
    float a = dot(dir, dir);
    float b = 2.0 * dot(dir, L);
    float c = dot(L, L) - radiusSquared;

    if (!solveQuadratic(a, b, c, t0, t1))
        return false;

    return t1 >= 0.0;
}

// Ray/sphere about the origin: (near, far) distances, or (1e30, -1e30) on a miss.
// `dir` must be normalized. The discriminant is computed as r^2 - |p - (p.d)d|^2, which
// keeps its precision when the origin is far from a small sphere.
vec2 raySphere(vec3 origin, vec3 dir, float radius) {
    float b = dot(origin, dir);
    vec3 closest = origin - b * dir;
    float d = radius * radius - dot(closest, closest);
    if (d < 0.0)
        return vec2(1e30, -1e30);
    d = sqrt(d);
    return vec2(-b - d, -b + d);
}

bool intersectPlane(vec3 n, vec3 p0, vec3 l0, vec3 l, out float t) {
    // Assuming vectors are all normalized
    t = 0.0;
    float denom = dot(n, l);
    if (denom > 1e-6) {
        vec3 p0l0 = p0 - l0;
        t = dot(p0l0, n) / denom;
        return (t >= 0.0);
    }

    return false;
}

// `hitDistance` is the distance from the disk centre to the hit point (the historical name
// in these shaders was `intersectSquared`, but it has always been the square root).
bool intersectDisk(vec3 n, vec3 p0, float radius, vec3 l0, vec3 l, out float hitDistance) {
    hitDistance = 0.0;
    float t = 0.0;
    if (intersectPlane(n, p0, l0, l, t)) {
        vec3 p = l0 + l * t;
        vec3 v = p - p0;
        float d2 = dot(v, v);
        hitDistance = sqrt(d2);
        return d2 <= radius * radius;
    }

    return false;
}
