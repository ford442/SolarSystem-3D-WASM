#ifndef SOLARSYSTEM_MESH_H
#define SOLARSYSTEM_MESH_H
#include "Shader.h"
#include "GlHandle.h"
#include "Vertex.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <assimp/types.h>
#include <string>
#include <utility>
#include <vector>

struct Texture {
    size_t id;
    std::string type;
    aiString path;
};

class Mesh {
public:
    explicit Mesh(std::vector<Vertex> vertices, std::vector<GLuint> indices, std::vector<Texture> textures);

    // Deep copy (creates new GL buffers)
    Mesh(const Mesh& other);
    Mesh& operator=(const Mesh& other);

    Mesh(Mesh&& other) noexcept = default;
    Mesh& operator=(Mesh&& other) noexcept = default;
    
    void Draw(const Shader& shader) const; // Отрисовка (рендеринг) меша

    /** Point attributes 0–4 of the bound VAO at Vertex fields in the bound GL_ARRAY_BUFFER. */
    static void SetVertexLayout();

private:
    std::vector<Vertex> _vertices; // Вершины
    std::vector<GLuint> _indices; // Индексы
    std::vector<Texture> _textures; // Текстуры
    GlBuffer _vbo; // Объект вершинного буфера (VBO)
    GlVertexArray _vao; // Объект вершинного массива (VAO)
    GlBuffer _ebo; // Объект элементного буфера (EBO)

    // Инициализация всех буферных объектов / массивов
    void SetupMesh();
};

#endif //SOLARSYSTEM_MESH_H
