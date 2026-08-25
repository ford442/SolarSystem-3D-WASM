#include "SimState.h"

SimState gFallbackSimState;
SimState* gSimState = &gFallbackSimState;

void ResetSimStateToFallback() {
    gSimState = &gFallbackSimState;
}
