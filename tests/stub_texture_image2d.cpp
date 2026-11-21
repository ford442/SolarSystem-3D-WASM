#include "gfx/TextureImage2D.h"

void TextureImage2D::ReloadTexture(const std::string& path, GLint wrapParam, GLint minFilter, GLint magFilter) {
    (void)path;
    (void)wrapParam;
    (void)minFilter;
    (void)magFilter;
}

// No GL here: the decode/upload is "done" the moment it is asked for, so the queue and LOD
// tests see the same synchronous completion they always did.
void TextureImage2D::ReloadTextureAsync(const std::string& path, std::function<void(AsyncResult)> done,
                                        std::function<bool()> stillWanted, GLint wrapParam, GLint minFilter,
                                        GLint magFilter) {
    (void)path;
    (void)wrapParam;
    (void)minFilter;
    (void)magFilter;
    if (done) {
        done(stillWanted && !stillWanted() ? AsyncResult::Superseded : AsyncResult::Uploaded);
    }
}
