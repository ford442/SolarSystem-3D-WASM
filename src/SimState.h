#ifndef SOLARSYSTEM_SIM_STATE_H
#define SOLARSYSTEM_SIM_STATE_H

struct SimState {
    float timeScale = 1.0f;
    bool timePaused = false;
    bool advanceStep = false;
    int shadowQuality = 3; // 0=off, 1=low, 2=medium, 3=full
    float simDeltaSeconds = 0.0f;
    int qualityPreset = 2;
    bool isMobileWeb = false;
};

/** Active simulation state; points at Application::_simState while the app runs. */
extern SimState* gSimState;

void ResetSimStateToFallback();

#endif // SOLARSYSTEM_SIM_STATE_H
