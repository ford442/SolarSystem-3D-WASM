#ifndef SOLARSYSTEM_GFX_GLPLATFORM_H
#define SOLARSYSTEM_GFX_GLPLATFORM_H

// The one place that picks the GL header for the build. Native links GLEW (desktop GL 4.6);
// Emscripten's GL/glew.h shim maps onto its WebGL 2 / GLES 3.0 bindings.
#include <GL/glew.h>

#ifdef __EMSCRIPTEN__
// Emscripten's glew/SDL_opengl declare DSA entry points that WebGL 2 does not
// implement. Macro (not inline) so -flto cannot emit an unresolved import.
//
// NOTE: this polyfill is GL_TEXTURE_2D ONLY, while the native glBindTextureUnit it
// stands in for is target-agnostic. A cube map (or any other target) bound through it
// works natively and silently binds nothing on web — see SkyBox::Render, which guards
// the call with #ifndef __EMSCRIPTEN__ and binds GL_TEXTURE_CUBE_MAP by hand. Treat this
// as `BindTexture2D` at every call site.
#undef glBindTextureUnit
#define glBindTextureUnit(unit, texture) \
    do { \
        glActiveTexture(GL_TEXTURE0 + (unit)); \
        glBindTexture(GL_TEXTURE_2D, (texture)); \
    } while (0)
#endif

#endif // SOLARSYSTEM_GFX_GLPLATFORM_H
