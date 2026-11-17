#ifndef SOLARSYSTEM_RENDER_MAGNETICFIELDTRACEJOBS_H
#define SOLARSYSTEM_RENDER_MAGNETICFIELDTRACEJOBS_H
#include "../Solar_System/OrbitLayout.h"
#include "../astro/MagneticFieldParams.h"
#include "../astro/MagneticFieldTracer.h"
#include "../core/JobSystem.h"
#include <vector>

/**
 * One field-line build in flight: a trace job per body (MagneticFieldTracer is GL-free), for
 * one quality preset. Renderer uploads the ribbons when every trace is in, so turning the
 * fields on (or a preset change) costs a frame of latency instead of a stall.
 */
struct MagneticFieldTraceJobs {
    struct Body {
        OrbitLayout::Body body;
        MagneticFieldParams params;
        Jobs::Future<std::vector<MagneticFieldLine>> lines;
    };
    int qualityPreset = -1;
    std::vector<Body> bodies;

    bool Ready() const {
        for (const Body& body : bodies) {
            if (!body.lines.Ready()) {
                return false;
            }
        }
        return true;
    }
};

#endif // SOLARSYSTEM_RENDER_MAGNETICFIELDTRACEJOBS_H
