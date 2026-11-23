#ifndef SOLARSYSTEM_GOLDENCAPTURE_H
#define SOLARSYSTEM_GOLDENCAPTURE_H
#ifndef __EMSCRIPTEN__
#include <string>

/**
 * SOLARSYSTEM_GOLDEN=frames=N,out=<file.ppm>[,step=s][,focus=i][,jd=d][,fields=1] (native):
 * load, draw N capture frames a fixed `step` apart (1/30 s by default) and write the last one's
 * back buffer to `out` (Application::RunGoldenCapture). With SOLARSYSTEM_JOBS=0 every input of
 * the frame is a function of these, so two builds can be compared pixel for pixel
 * (scripts/golden-native.sh, scripts/golden-compare.py).
 */
struct GoldenSpec {
    int frames = 60;
    double step = 1.0 / 30.0;
    int focus = -1;  // FocusPlanetByIndex before the first frame
    double jd = 0.0; // pinned (and paused) date; also the date the scene is built at
    bool fields = false;
    std::string out = "golden.ppm";
};

GoldenSpec ParseGoldenSpec(const std::string& spec);
/** SOLARSYSTEM_GOLDEN's `jd` (0 without one): the date Application starts its clock at. */
double GoldenStartJulianDate();
#endif

#endif // SOLARSYSTEM_GOLDENCAPTURE_H
