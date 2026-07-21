#ifndef SOLARSYSTEM_SYSTEMMODULES_H
#define SOLARSYSTEM_SYSTEMMODULES_H

// #include <GL/wglext.h>

#ifdef __EMSCRIPTEN__
    #include <GLES3/gl3.h>
    #include <GLFW/glfw3.h>
    #include <SDL2/SDL.h>
    #include <SDL2/SDL_mixer.h>

    // Polyfill for OpenGL 4.5+ Direct State Access (DSA).
    // WebGL 2 (OpenGL ES 3.0) does not support DSA. Use a macro so LTO cannot
    // resolve calls to a missing GL import (glext may declare the symbol).
    // Assumes GL_TEXTURE_2D. For cube maps, bind manually.
    #undef glBindTextureUnit
    #define glBindTextureUnit(unit, texture) \
        do { \
            glActiveTexture(GL_TEXTURE0 + (unit)); \
            glBindTexture(GL_TEXTURE_2D, (texture)); \
        } while (0)
#else
    #include <GL/glew.h>
    #include <GLFW/glfw3.h>
    #ifdef SOLARSYSTEM_USE_SDL_MIXER
        #include <SDL2/SDL_mixer.h>
    #else
        #include <irrKlang.h>
        using namespace irrklang;
    #endif
#endif

#include <ft2build.h>
#include FT_FREETYPE_H

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <memory>

#endif //SOLARSYSTEM_SYSTEMMODULES_H
