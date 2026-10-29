#ifndef SOLARSYSTEM_MESHKEEPER_H
#define SOLARSYSTEM_MESHKEEPER_H
#include "Shader.h"
#include "Mesh.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <memory>
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <vector>

size_t TextureFromFile(const std::string& path); // Not used at all

class MeshHolder {
public:
    explicit MeshHolder(const std::string& path);
    void Draw(const Shader& shader) const; // Отрисовка модели (мешей)
    /** Largest vertex distance from the model origin, in model units (culling bounds). */
    float BoundingRadius() const { return _boundingRadius; }
    /**
     * Every mesh's vertices and triangle indices, kept on the CPU for the camera floor's ray
     * casts (positions, GL-free for core/) and the near-surface patch (whole vertices).
     */
    struct Triangles {
        std::vector<glm::vec3> positions;
        std::vector<Vertex> vertices;
        std::vector<unsigned> indices;
        float maxEdge = 0.0f; // longest triangle edge, model units
    };
    /** Shared by copies: every body holds its own MeshHolder copy of sphere.obj. */
    const Triangles& GetTriangles() const { return *_triangles; }

private:
    std::shared_ptr<Triangles> _triangles = std::make_shared<Triangles>();
    std::vector<Texture> _loadedTextures;
    std::vector<Mesh> _meshes;
    std::string _directory;
    float _boundingRadius = 0.0f;

    // Обрабатывает узел рекурсивно. Обрабатывает каждую отдельную сетку, расположенную в узле, и повторяет этот процесс на своих дочерних узлах (если есть).
    void ProcessNode(aiNode* node, const aiScene* scene);
    Mesh ProcessMesh(aiMesh* mesh, const aiScene* scene);
    std::vector<Texture> LoadMaterialTextures(aiMaterial* mat, const aiTextureType& type, const std::string& typeName);
};

#endif //SOLARSYSTEM_MESHKEEPER_H
