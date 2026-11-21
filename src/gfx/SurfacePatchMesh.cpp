#include "SurfacePatchMesh.h"

#include "Mesh.h"

void SurfacePatchMesh::Stream(const std::vector<Vertex>& vertices) {
    const int count = static_cast<int>(vertices.size());
    if (!_vao || count > _capacity) {
        // Room to grow, so a patch that gains a few pieces as the camera moves reuses it.
        _capacity = count + count / 2;
        _vao = GlVertexArray::Create();
        _vbo = GlBuffer::Create();
        glBindVertexArray(_vao.get());
        glBindBuffer(GL_ARRAY_BUFFER, _vbo.get());
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(_capacity * sizeof(Vertex)), nullptr, GL_DYNAMIC_DRAW);
        Mesh::SetVertexLayout();
        glBindVertexArray(0);
    }
    if (count > 0) {
        glBindBuffer(GL_ARRAY_BUFFER, _vbo.get());
        glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(count * sizeof(Vertex)), vertices.data());
    }
    _vertexCount = count;
}

void SurfacePatchMesh::Draw() const {
    if (!_vao || _vertexCount <= 0) {
        return;
    }
    glBindVertexArray(_vao.get());
    glDrawArrays(GL_TRIANGLES, 0, _vertexCount);
    glBindVertexArray(0);
}
