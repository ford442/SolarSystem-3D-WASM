#ifndef SOLARSYSTEM_SIM_STATE_H
#define SOLARSYSTEM_SIM_STATE_H

/**
 * Clock and quality state owned by Application (`_simState`) and handed explicitly to what
 * needs it: the renderer through FrameContext, LOD through TextureLodPolicy. There is no
 * global instance.
 */
struct SimState {
    float timeScale = 1.0f;
    bool timePaused = false;
    bool advanceStep = false;
    int shadowQuality = 3; // 0=off, 1=low, 2=medium, 3=full
    float simDeltaSeconds = 0.0f;
    /**
     * Seconds of animation since start: the sum of frame deltas, so shader time (FrameUBO
     * `time`) and the field-line ribbons run on the frame clock. A capture steps it by a
     * fixed dt per frame instead of the wall clock (Application::StepCaptureFrame).
     */
    double animationSeconds = 0.0;
    int qualityPreset = 2;
    bool isMobileWeb = false;
    /** Canvas backing-store pixels per CSS pixel (capped per quality tier); see webglContext.ts. */
    float backingStoreScale = 1.0f;
    /** glGetIntegerv(GL_SAMPLES) of the default framebuffer, read once after context creation. */
    int contextSamples = 0;
};

#endif // SOLARSYSTEM_SIM_STATE_H
