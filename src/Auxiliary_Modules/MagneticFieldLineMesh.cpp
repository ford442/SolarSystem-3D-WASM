#include "MagneticFieldLineMesh.h"

#include "Shader.h"
#include <cstddef>

MagneticFieldLineMesh::~MagneticFieldLineMesh() {
    Destroy();
}

MagneticFieldLineMesh::MagneticFieldLineMesh(MagneticFieldLineMesh&& other) noexcept
    : _vao(other._vao), _vbo(other._vbo), _vertexCount(other._vertexCount) {
    other._vao = 0;
    other._vbo = 0;
    other._vertexCount = 0;
}

MagneticFieldLineMesh& MagneticFieldLineMesh::operator=(MagneticFieldLineMesh&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    Destroy();
    _vao = other._vao;
    _vbo = other._vbo;
    _vertexCount = other._vertexCount;
    other._vao = 0;
    other._vbo = 0;
    other._vertexCount = 0;
    return *this;
}

void MagneticFieldLineMesh::Destroy() {
    if (_vbo) {
        glDeleteBuffers(1, &_vbo);
        _vbo = 0;
    }
    if (_vao) {
        glDeleteVertexArrays(1, &_vao);
        _vao = 0;
    }
    _vertexCount = 0;
}

void MagneticFieldLineMesh::Upload(const std::vector<MagneticFieldLine>& lines) {
    UploadVertices(Expand(lines));
}

void MagneticFieldLineMesh::UploadVertices(const std::vector<MagneticFieldRibbonVertex>& verts) {
    Destroy();
    if (verts.empty()) {
        return;
    }

    _vertexCount = static_cast<int>(verts.size());
    glGenVertexArrays(1, &_vao);
    glGenBuffers(1, &_vbo);
    glBindVertexArray(_vao);
    glBindBuffer(GL_ARRAY_BUFFER, _vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * sizeof(MagneticFieldRibbonVertex)),
                 verts.data(), GL_STATIC_DRAW);

    const GLsizei stride = static_cast<GLsizei>(sizeof(MagneticFieldRibbonVertex));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(offsetof(MagneticFieldRibbonVertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(offsetof(MagneticFieldRibbonVertex, tangent)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(offsetof(MagneticFieldRibbonVertex, arcLength)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(offsetof(MagneticFieldRibbonVertex, lineUV)));

    glBindVertexArray(0);
}

void MagneticFieldLineMesh::Draw() const {
    if (_vao == 0 || _vertexCount <= 0) {
        return;
    }
    glBindVertexArray(_vao);
    glDrawArrays(GL_TRIANGLES, 0, _vertexCount);
    glBindVertexArray(0);
}
