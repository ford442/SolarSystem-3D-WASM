#include "SkyBox.h"
#include "../Auxiliary_Modules/WebResourceFetcher.h"

SkyBox::SkyBox(const std::vector<std::string>& faces) {
    InitBuffers();

    // Ensure all 6 faces are available before loading
    for (const auto& face : faces) {
        WebResourceFetcher::Fetch(face);
    }

    LoadCubeMap(faces);
}

void SkyBox::Render(const Shader& shader) const {
    glDepthFunc(GL_LEQUAL);

    shader.Use();
    shader.SetInt("skybox", 0);
#ifndef __EMSCRIPTEN__
    glBindTextureUnit(0, _textureID);
#endif

    glBindVertexArray(_vao);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, _textureID);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glBindVertexArray(0);

    glDepthFunc(GL_LESS);
}

void SkyBox::LoadCubeMap(const std::vector<std::string>& faces) {
    glGenTextures(1, &_textureID);
    glBindTexture(GL_TEXTURE_CUBE_MAP, _textureID);

#ifdef __EMSCRIPTEN__
    // Track whether any face fails so we can fall back to a solid-colour cubemap.
    // A cubemap with mismatched dimensions/formats across faces is incomplete in
    // WebGL 2, which would silently render black.  It is safer to rebuild every
    // face as a matching 1×1 black texel when the face DDS files are unavailable.
    bool anyFailed = false;
    for (size_t i = 0; i < faces.size(); i++) {
        GLenum target = GL_TEXTURE_CUBE_MAP_POSITIVE_X + static_cast<GLenum>(i);
        try {
            CDDSImage image;
            image.load(faces[i], false);
            image.upload_texture2D(0, target);
            std::cout << faces[i] << " Loaded" << std::endl;
        }
        catch (const std::runtime_error& error) {
            std::cerr << "[SkyBox] Warning: failed to load face " << faces[i]
                      << " (" << error.what() << "). Using fallback cubemap." << std::endl;
            anyFailed = true;
            break;
        }
    }

    if (anyFailed) {
        // Rebuild all six faces as a consistent 1×1 black RGBA cubemap so WebGL 2
        // considers the texture complete and the scene continues to render.
        glDeleteTextures(1, &_textureID);
        glGenTextures(1, &_textureID);
        glBindTexture(GL_TEXTURE_CUBE_MAP, _textureID);
        static const uint8_t black[4] = {0u, 0u, 0u, 255u};
        for (unsigned int face = 0; face < 6u; ++face) {
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_RGBA8,
                         1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, black);
        }
        // Cap at level 0 – no mipmaps on fallback.
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_LEVEL, 0);
    }
#else
    for (size_t i = 0; i < faces.size(); i++) {
        try {
            CDDSImage image;
            GLenum target = GL_TEXTURE_CUBE_MAP_POSITIVE_X + static_cast<GLenum>(i);
            image.load(faces[i], false);
            image.upload_texture2D(0, target);
            std::cout << faces[i] << " Loaded" << std::endl;
        }
        catch (const std::runtime_error& error) {
            throw std::runtime_error("Image " + faces[i] + " cannot be loaded");
        }
    }
#endif

    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
}

void SkyBox::InitBuffers() {
    constexpr float skyboxVertices[] = {
             // Positions
            -1.0f,  1.0f, -1.0f,
            -1.0f, -1.0f, -1.0f,
             1.0f, -1.0f, -1.0f,
             1.0f, -1.0f, -1.0f,
             1.0f,  1.0f, -1.0f,
            -1.0f,  1.0f, -1.0f,

            -1.0f, -1.0f,  1.0f,
            -1.0f, -1.0f, -1.0f,
            -1.0f,  1.0f, -1.0f,
            -1.0f,  1.0f, -1.0f,
            -1.0f,  1.0f,  1.0f,
            -1.0f, -1.0f,  1.0f,

             1.0f, -1.0f, -1.0f,
             1.0f, -1.0f,  1.0f,
             1.0f,  1.0f,  1.0f,
             1.0f,  1.0f,  1.0f,
             1.0f,  1.0f, -1.0f,
             1.0f, -1.0f, -1.0f,

            -1.0f, -1.0f,  1.0f,
            -1.0f,  1.0f,  1.0f,
             1.0f,  1.0f,  1.0f,
             1.0f,  1.0f,  1.0f,
             1.0f, -1.0f,  1.0f,
            -1.0f, -1.0f,  1.0f,

            -1.0f,  1.0f, -1.0f,
             1.0f,  1.0f, -1.0f,
             1.0f,  1.0f,  1.0f,
             1.0f,  1.0f,  1.0f,
            -1.0f,  1.0f,  1.0f,
            -1.0f,  1.0f, -1.0f,

            -1.0f, -1.0f, -1.0f,
            -1.0f, -1.0f,  1.0f,
             1.0f, -1.0f, -1.0f,
             1.0f, -1.0f, -1.0f,
            -1.0f, -1.0f,  1.0f,
             1.0f, -1.0f,  1.0f
    };

    glGenVertexArrays(1, &_vao);
    glGenBuffers(1, &_vbo);
    glBindVertexArray(_vao);
    glBindBuffer(GL_ARRAY_BUFFER, _vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(skyboxVertices), &skyboxVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)nullptr);
}
