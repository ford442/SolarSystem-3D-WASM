#ifndef SOLARSYSTEM_SIM_STATE_H
#define SOLARSYSTEM_SIM_STATE_H

extern float gTimeScale;
extern bool gTimePaused;
extern bool gAdvanceStep;
extern int gShadowQuality;

/** Scaled sim seconds for this frame (0 when paused). Set once in RunOneFrame. */
extern float gSimDeltaSeconds;

#endif // SOLARSYSTEM_SIM_STATE_H
