#ifndef SOLARSYSTEM_WASM_EXPORTS_H
#define SOLARSYSTEM_WASM_EXPORTS_H

class Application;

Application* GetActiveApplication();
void SetActiveApplication(Application* application);

#ifdef __EMSCRIPTEN__
float GetTouchForward();
float GetTouchRight();
float GetTouchVertical();
#endif

#endif // SOLARSYSTEM_WASM_EXPORTS_H
