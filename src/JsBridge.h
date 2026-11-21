#ifndef SOLARSYSTEM_JS_BRIDGE_H
#define SOLARSYSTEM_JS_BRIDGE_H

#ifdef __EMSCRIPTEN__

void NotifySettingsChanged(const char* field);
void NotifyLoadingProgress(int loaded, int total, int failed);
void NotifyStreamingProgress(int completed, int total, int active, int tierCode);
void NotifyPlanetFocused(int index);
void NotifyMusicDuck(float duck);
void NotifyNearestPlanetChanged(int jsIndex);
void NotifyMissionFocused(int index);
void NotifySystemChanged(const char* id, int phase);

#else

inline void NotifySettingsChanged(const char*) {}
inline void NotifyLoadingProgress(int, int, int) {}
inline void NotifyStreamingProgress(int, int, int, int) {}
inline void NotifyPlanetFocused(int) {}
inline void NotifyMusicDuck(float) {}
inline void NotifyNearestPlanetChanged(int) {}
inline void NotifyMissionFocused(int) {}
inline void NotifySystemChanged(const char*, int) {}

#endif

#endif // SOLARSYSTEM_JS_BRIDGE_H
