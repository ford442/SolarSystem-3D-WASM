#include "JsBridge.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

void NotifySettingsChanged(const char* field) {
    EM_ASM({
        if (typeof Module.onSettingsChanged === 'function') {
            Module.onSettingsChanged(UTF8ToString($0));
        }
    }, field);
}

void NotifyLoadingProgress(int loaded, int total) {
    EM_ASM({
        if (typeof Module.updateLoadingProgress === 'function') {
            Module.updateLoadingProgress($0, $1);
        }
    }, loaded, total);
}

void NotifyStreamingProgress(int completed, int total, int active, int tierCode) {
    EM_ASM({
        if (typeof Module.updateStreamingProgress === 'function') {
            Module.updateStreamingProgress($0, $1, $2, $3);
        }
        if (typeof Module.updateLoadingProgress === 'function' && $1 > 0) {
            Module.updateLoadingProgress($0, $1);
        }
    }, completed, total, active, tierCode);
}

void NotifyPlanetFocused(int index) {
    EM_ASM({
        if (typeof Module.onPlanetFocused === 'function') {
            Module.onPlanetFocused($0);
        }
    }, index);
}

#endif
