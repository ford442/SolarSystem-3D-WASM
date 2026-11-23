#include "MeshHolder.h"
#include "../assets/WebResourceFetcher.h"
#include "../assets/ModelCache.h"
#include "Mesh.h"
#include <assimp/types.h>
#include <memory>
#include <vector>
#include <utility>

MeshHolder::MeshHolder(const std::string& path) {
    // Models are downloaded by Application::LoadCoreResources() before the scene is
    // constructed (and their parse started as each landed); this only reports a download
    // that never did, since Assimp's failure is reported as a generic "unable to open file".
    WebResourceFetcher::RequireResident(path, "MeshHolder");

    const std::shared_ptr<const MeshPayload> payload = ModelCache::Get(path);
    _boundingRadius = payload->boundingRadius;
    _triangles = payload->triangles;
    _meshes.reserve(payload->parts.size());
    for (const MeshPayload::Part& part : payload->parts) {
        std::vector<Texture> textures;
        textures.reserve(part.textures.size());
        for (const MeshPayload::Material& material : part.textures) {
            textures.push_back(Texture{0, material.type, aiString(material.path)});
        }
        _meshes.emplace_back(part.vertices, part.indices, std::move(textures));
    }
}

void MeshHolder::Draw(const Shader& shader) const {
    for (const auto& mesh : _meshes)
        mesh.Draw(shader);
}
