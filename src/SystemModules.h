#ifndef SOLARSYSTEM_SYSTEMMODULES_H
#define SOLARSYSTEM_SYSTEMMODULES_H

#ifdef __EMSCRIPTEN__
    #include <GLES3/gl3.h>
#endif
// GL header and the web glBindTextureUnit polyfill: one copy, in gfx/GlPlatform.h.
#include "gfx/GlPlatform.h"
#include <GLFW/glfw3.h>

#ifdef __EMSCRIPTEN__
    #include <SDL2/SDL.h>
#elif defined(SOLARSYSTEM_USE_SDL_MIXER)
    #include <SDL2/SDL_mixer.h>
#else
    #include <irrKlang.h>
#endif

#include <ft2build.h>
#include FT_FREETYPE_H

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <memory>

#endif //SOLARSYSTEM_SYSTEMMODULES_H
