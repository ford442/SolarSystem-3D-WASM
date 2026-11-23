#include "ModelCache.h"
#include "../core/JobSystem.h"
#include "../gfx/Vertex.h"
#include "TextureDecode.h"
#include "WebResourceFetcher.h"
#include <algorithm>
#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/mesh.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <glm/geometric.hpp>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

void AppendMaterialTextures(const aiMaterial& material, aiTextureType type, const char* typeName,
                            std::vector<MeshPayload::Material>& out) {
    for (unsigned i = 0; i < material.GetTextureCount(type); ++i) {
        aiString name;
        material.GetTexture(type, i, &name);
        out.push_back({typeName, name.C_Str()});
    }
}

MeshPayload::Part ConvertMesh(const aiMesh& mesh, const aiScene& scene, MeshPayload& payload,
                              MeshTriangles& triangles) {
    MeshPayload::Part part;
    part.vertices.reserve(mesh.mNumVertices);
    for (unsigned i = 0; i < mesh.mNumVertices; ++i) {
        Vertex vertex{};
        vertex.position = glm::vec3(mesh.mVertices[i].x, mesh.mVertices[i].y, mesh.mVertices[i].z);
        payload.boundingRadius = std::max(payload.boundingRadius, glm::length(vertex.position));
        if (mesh.HasNormals()) {
            vertex.normal = glm::vec3(mesh.mNormals[i].x, mesh.mNormals[i].y, mesh.mNormals[i].z);
        }
        if (mesh.mTextureCoords[0]) {
            vertex.textureCoords = glm::vec2(mesh.mTextureCoords[0][i].x, mesh.mTextureCoords[0][i].y);
            vertex.tangent = glm::vec3(mesh.mTangents[i].x, mesh.mTangents[i].y, mesh.mTangents[i].z);
            vertex.bitangent = glm::vec3(mesh.mBitangents[i].x, mesh.mBitangents[i].y, mesh.mBitangents[i].z);
        } else {
            vertex.textureCoords = glm::vec2(0.0f, 0.0f);
        }
        part.vertices.push_back(vertex);
    }
    for (unsigned i = 0; i < mesh.mNumFaces; ++i) {
        const aiFace& face = mesh.mFaces[i];
        for (unsigned j = 0; j < face.mNumIndices; ++j) {
            part.indices.push_back(face.mIndices[j]);
        }
    }

    const auto base = static_cast<unsigned>(triangles.positions.size());
    for (const Vertex& vertex : part.vertices) {
        triangles.positions.push_back(vertex.position);
        triangles.vertices.push_back(vertex);
    }
    for (const unsigned index : part.indices) {
        triangles.indices.push_back(base + index);
    }
    for (std::size_t i = 0; i + 2 < part.indices.size(); i += 3) {
        const glm::vec3& a = part.vertices[part.indices[i]].position;
        const glm::vec3& b = part.vertices[part.indices[i + 1]].position;
        const glm::vec3& c = part.vertices[part.indices[i + 2]].position;
        triangles.maxEdge =
            std::max({triangles.maxEdge, glm::distance(a, b), glm::distance(b, c), glm::distance(c, a)});
    }

    // Material texture names are carried as they always were (Mesh::Draw binds them); the
    // tree's models have none.
    const aiMaterial& material = *scene.mMaterials[mesh.mMaterialIndex];
    AppendMaterialTextures(material, aiTextureType_DIFFUSE, "texture_diffuse", part.textures);
    AppendMaterialTextures(material, aiTextureType_SPECULAR, "texture_specular", part.textures);
    AppendMaterialTextures(material, aiTextureType_HEIGHT, "texture_normal", part.textures);
    AppendMaterialTextures(material, aiTextureType_AMBIENT, "texture_height", part.textures);
    return part;
}

void ConvertNode(const aiNode& node, const aiScene& scene, MeshPayload& payload, MeshTriangles& triangles) {
    for (unsigned i = 0; i < node.mNumMeshes; ++i) {
        payload.parts.push_back(ConvertMesh(*scene.mMeshes[node.mMeshes[i]], scene, payload, triangles));
    }
    for (unsigned i = 0; i < node.mNumChildren; ++i) {
        ConvertNode(*node.mChildren[i], scene, payload, triangles);
    }
}

struct Entry {
    Jobs::Future<MeshPayload> pending;
    std::shared_ptr<const MeshPayload> parsed;
};

std::unordered_map<std::string, Entry>& Entries() {
    static std::unordered_map<std::string, Entry> entries;
    return entries;
}

std::string Extension(const std::string& path) {
    const std::size_t dot = path.find_last_of('.');
    return dot == std::string::npos ? std::string() : path.substr(dot + 1);
}

} // namespace

MeshPayload ParseModel(const std::string& path, const std::vector<std::uint8_t>& bytes) {
    // One Importer per call: Assimp importers are independent, so jobs parse in parallel.
    Assimp::Importer importer;
    const std::string hint = Extension(path);
    const aiScene* scene = importer.ReadFileFromMemory(bytes.data(), bytes.size(),
                                                       aiProcess_Triangulate | aiProcess_GenSmoothNormals |
                                                           aiProcess_FlipUVs | aiProcess_CalcTangentSpace,
                                                       hint.c_str());
    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode) {
        throw std::runtime_error("ERROR::ASSIMP:: " + path + ": " + importer.GetErrorString());
    }
    MeshPayload payload;
    auto triangles = std::make_shared<MeshTriangles>();
    ConvertNode(*scene->mRootNode, *scene, payload, *triangles);
    payload.triangles = std::move(triangles);
    return payload;
}

namespace ModelCache {

void Prefetch(const std::string& path) {
    auto& entries = Entries();
    if (entries.count(path) || !WebResourceFetcher::ResourceExists(path)) {
        return;
    }
    // Bytes are read here: MEMFS belongs to the main thread.
    std::vector<std::uint8_t> bytes;
    try {
        bytes = ReadTextureFile(path);
    } catch (const std::exception&) {
        return; // Get() reports it, at the read site that needs the model
    }
    entries[path].pending = Jobs::Submit([path, bytes = std::move(bytes)] { return ParseModel(path, bytes); });
}

std::shared_ptr<const MeshPayload> Get(const std::string& path) {
    auto& entries = Entries();
    auto it = entries.find(path);
    if (it == entries.end() || (!it->second.parsed && !it->second.pending.Valid())) {
        Prefetch(path);
        it = entries.find(path);
        if (it == entries.end()) {
            throw std::runtime_error("ERROR::ASSIMP:: unable to open " + path);
        }
    }
    Entry& entry = it->second;
    if (!entry.parsed) {
        try {
            entry.parsed = std::make_shared<const MeshPayload>(entry.pending.Get());
        } catch (...) {
            entries.erase(it); // a later Get retries (e.g. after the file is re-downloaded)
            throw;
        }
    }
    return entry.parsed;
}

void Clear() {
    Entries().clear();
}

} // namespace ModelCache
