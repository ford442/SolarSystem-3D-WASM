#include "TerrainRenderer.h"

#include "../../gfx/Shader.h"
#include "../../gfx/TextureUnits.h"

namespace {

using Terrain::kTileQuads;
using Terrain::kTileSamples;
using Terrain::kTileStride;

/** Grid vertex (x, y); skirt vertices follow, one row of kTileSamples per edge: bottom, top, left, right. */
GLushort GridIndex(int x, int y) {
    return static_cast<GLushort>(y * kTileSamples + x);
}

GLushort SkirtIndex(int edge, int i) {
    return static_cast<GLushort>(kTileSamples * kTileSamples + edge * kTileSamples + i);
}

} // namespace

void TerrainRenderer::BuildGrid() {
    std::vector<glm::vec3> vertices;
    vertices.reserve(static_cast<std::size_t>(kTileSamples * kTileSamples + 4 * kTileSamples));
    for (int y = 0; y <= kTileQuads; ++y) {
        for (int x = 0; x <= kTileQuads; ++x) {
            vertices.emplace_back(x, y, 0.0f);
        }
    }
    // Skirts: the edge vertices again, which terrain.vs lowers by the tile's skirt depth.
    for (int i = 0; i <= kTileQuads; ++i) vertices.emplace_back(i, 0, 1.0f);
    for (int i = 0; i <= kTileQuads; ++i) vertices.emplace_back(i, kTileQuads, 1.0f);
    for (int i = 0; i <= kTileQuads; ++i) vertices.emplace_back(0, i, 1.0f);
    for (int i = 0; i <= kTileQuads; ++i) vertices.emplace_back(kTileQuads, i, 1.0f);

    std::vector<GLushort> indices;
    indices.reserve(static_cast<std::size_t>(kTileQuads * kTileQuads * 6 + 4 * kTileQuads * 12));
    // Counter-clockwise from outside (u × v = n), each quad split along (x, y)–(x+1, y+1):
    // Terrain::SurfaceHeight and the morph to the parent assume this diagonal.
    for (int y = 0; y < kTileQuads; ++y) {
        for (int x = 0; x < kTileQuads; ++x) {
            indices.insert(indices.end(), {GridIndex(x, y), GridIndex(x + 1, y), GridIndex(x + 1, y + 1),
                                           GridIndex(x, y), GridIndex(x + 1, y + 1), GridIndex(x, y + 1)});
        }
    }
    // Each skirt quad in both windings: back faces are culled, and a skirt is seen from either side.
    const auto skirt = [&indices](GLushort a, GLushort b, GLushort lowA, GLushort lowB) {
        indices.insert(indices.end(), {a, b, lowB, a, lowB, lowA, a, lowB, b, a, lowA, lowB});
    };
    for (int i = 0; i < kTileQuads; ++i) {
        skirt(GridIndex(i, 0), GridIndex(i + 1, 0), SkirtIndex(0, i), SkirtIndex(0, i + 1));
        skirt(GridIndex(i, kTileQuads), GridIndex(i + 1, kTileQuads), SkirtIndex(1, i), SkirtIndex(1, i + 1));
        skirt(GridIndex(0, i), GridIndex(0, i + 1), SkirtIndex(2, i), SkirtIndex(2, i + 1));
        skirt(GridIndex(kTileQuads, i), GridIndex(kTileQuads, i + 1), SkirtIndex(3, i), SkirtIndex(3, i + 1));
    }
    _indexCount = static_cast<int>(indices.size());

    _vao = GlVertexArray::Create();
    _vertices = GlBuffer::Create();
    _indices = GlBuffer::Create();
    glBindVertexArray(_vao.get());
    glBindBuffer(GL_ARRAY_BUFFER, _vertices.get());
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(glm::vec3)), vertices.data(),
                 GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), nullptr);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _indices.get());
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices.size() * sizeof(GLushort)), indices.data(),
                 GL_STATIC_DRAW);
    glBindVertexArray(0);
}

GLuint TerrainRenderer::TextureFor(const Terrain::HeightTile& tile) {
    GlTexture& texture = _textures[tile.uid];
    if (!texture) {
        GLint previous = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);
        texture = GlTexture::Create();
        glBindTexture(GL_TEXTURE_2D, texture.get());
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, kTileStride, kTileStride, 0, GL_RED, GL_FLOAT, tile.heights.data());
        // texelFetch only: R32F is not filterable on WebGL 2 without an extension.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous));
    }
    return texture.get();
}

int TerrainRenderer::Draw(const Shader& shader, const Terrain::ActiveBody& body, const glm::mat4& model, Pass pass) {
    if (!_vao) {
        BuildGrid();
    }
    const Terrain::BodyFrame& frame = body.frame;
    shader.SetMat4("model", model);
    glBindVertexArray(_vao.get());
    int drawn = 0;
    for (const Terrain::DrawTile& draw : body.selection.tiles) {
        const bool wanted = pass == Pass::Shadow ? draw.inLight
                                                 : draw.inView && draw.nearSurface == (pass == Pass::ColourNear);
        if (!wanted) {
            continue;
        }
        const Terrain::TileKey& key = draw.tile->key;
        const Terrain::FaceBasis basis = Terrain::Basis(key.face);
        const Terrain::TileFrame& tile = draw.frame;
        shader.SetMat3("tileBasis", glm::mat3(glm::vec3(basis.n), glm::vec3(basis.u), glm::vec3(basis.v)));
        shader.SetVec4("tileWarp", glm::vec4(tile.centerWarp, tile.warpCorrection));
        shader.SetVec4("tileTrig", glm::vec4(tile.centerCos, tile.centerSin));
        shader.SetVec4("tileShape", glm::vec4(tile.size, static_cast<float>(frame.meshRadius),
                                              static_cast<float>(frame.heightScale), static_cast<float>(draw.skirtDepth)));
        shader.SetVec3("tileOrigin", glm::vec3(draw.center));
        shader.SetVec2("tileMorph", glm::vec2(draw.morph));
        const int q = key.Quadrant();
        shader.SetVec2("tileParentOffset", glm::vec2(kTileQuads / 2 * (q & 1), kTileQuads / 2 * ((q >> 1) & 1)));
        const GLuint own = TextureFor(*draw.tile);
        glBindTextureUnit(Unit(TextureUnit::TerrainHeight), own);
        glBindTextureUnit(Unit(TextureUnit::TerrainParent), draw.parent ? TextureFor(*draw.parent) : own);
        glDrawElements(GL_TRIANGLES, _indexCount, GL_UNSIGNED_SHORT, nullptr);
        ++drawn;
    }
    glBindVertexArray(0);
    return drawn;
}

void TerrainRenderer::Forget(const std::vector<uint64_t>& uids) {
    for (const uint64_t uid : uids) {
        _textures.erase(uid);
    }
}
