#include "Application.h"
#include <iostream>

using namespace std;

// https://gist.github.com/statico/6809850727c708f08458
#ifdef _WIN32
#include <windows.h>
// Use discrete GPU by default.
extern "C" {
    __declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

int main(int, char**) {
    // setlocale can sometimes cause issues in WebAssembly depending on environment,
    // but usually fine. If text encoding is weird, try commenting this out.
    setlocale(LC_ALL, "RUS");

#ifdef __EMSCRIPTEN__
    // Heap-allocate so a returning Exec() (possible with -fwasm-exceptions +
    // emscripten_set_main_loop) cannot run Application::~Application and clear
    // WasmExports' activeApplication while the browser main loop still renders.
    try {
        auto* application = new Application();
        application->Exec();
    }
    catch (const exception& err) {
        cerr << "FATAL ERROR: " << err.what() << endl;
        return 1;
    }
    return 0;
#else
    try {
        Application application;
        application.Exec();
    }
    catch (const exception& err) {
        cerr << "FATAL ERROR: " << err.what() << endl;
        return 1;
    }

    return 0;
#endif
}
