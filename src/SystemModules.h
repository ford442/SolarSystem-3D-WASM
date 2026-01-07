#ifndef SOLARSYSTEM_SYSTEMMODULES_H
#define SOLARSYSTEM_SYSTEMMODULES_H

// #include <GL/wglext.h>

#ifdef __EMSCRIPTEN__
    #include <GLES3/gl3.h>
    #include <GLFW/glfw3.h>
    #include <SDL2/SDL.h>
    #include <SDL2/SDL_mixer.h>

    // Polyfill for OpenGL 4.5+ Direct State Access (DSA) function
    // WebGL 2 (OpenGL ES 3.0) does not support DSA.
    // Assumes GL_TEXTURE_2D target. For CubeMaps, handle manually.
    inline void glBindTextureUnit(GLuint unit, GLuint texture) {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, texture);
    }
#else
    #include <GL/glew.h>
    #include <GLFW/glfw3.h>
    #include <irrKlang.h>
    using namespace irrklang;
#endif

#include <ft2build.h>
#include FT_FREETYPE_H

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <memory>

#endif //SOLARSYSTEM_SYSTEMMODULES_H
