#ifndef SOLARSYSTEM_TEXTRENDERER_H
#define SOLARSYSTEM_TEXTRENDERER_H
#include "../gfx/Shader.h"
#include "../gfx/GlHandle.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <deque>
#include <map>

struct Character {
    GlTexture texture;      // Текстура глифа
    glm::ivec2 glyphSize;   // Размер глифа
    glm::ivec2 bearing;     // Смещение от линии шрифта до верхнего/левого угла глифа
    int advance;            // Смещение до следующего глифа
};

class TextRenderer : public IGpuRestorable {
public:
    /** Loads every glyph from `ft` now; `ft` may be released once this returns. */
    explicit TextRenderer(FT_Library ft,  const std::string& fontPath);
    TextRenderer(const TextRenderer&) = delete;
    TextRenderer& operator=(const TextRenderer&) = delete;
    /** After a lost context: re-rasterise the glyphs (a FreeType of its own) and the quad. */
    void RestoreGpu() override;
    void Render(const Shader& shader, const std::wstring& text, float x, float y, float scale, const glm::vec3& color);
    void Render(const Shader& shader, const std::deque<wchar_t>& text, float x, float y, float scale, const glm::vec3& color);
    void Render(const Shader& shader, const std::deque<std::wstring>& text, float x, float y, float scale, const glm::vec3& color);
    void ReverseRender(const Shader& shader, const std::deque<std::wstring>& text, float x, float y, float scale, const glm::vec3& color);

private:
    FT_Library _ft; // only during construction or RestoreGpu
    std::string _fontPath;
    std::map<wchar_t, Character> _characters;
    GlVertexArray _vao; // Объект вершинного массива (VAO)
    GlBuffer _vbo; // Объект вершинного буфера (VBO)

    void LoadFont(const std::string& path);
    void InitBuffers();
};

#endif //SOLARSYSTEM_TEXTRENDERER_H
