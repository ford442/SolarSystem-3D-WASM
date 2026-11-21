#include "LineStripBatch.h"

#include <cstddef>

void LineStripBatch::Allocate(int capacity, const RibbonVertex* data, GLenum usage) {
    _vao = GlVertexArray::Create();
    _vbo = GlBuffer::Create();
    _capacity = capacity;
    glBindVertexArray(_vao.get());
    glBindBuffer(GL_ARRAY_BUFFER, _vbo.get());
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(capacity * sizeof(RibbonVertex)), data, usage);
    const GLsizei stride = static_cast<GLsizei>(sizeof(RibbonVertex));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(RibbonVertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(RibbonVertex, tangent)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(RibbonVertex, arcLength)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(RibbonVertex, lineUV)));
    glBindVertexArray(0);
}

void LineStripBatch::Upload(const std::vector<RibbonVertex>& verts) {
    Clear();
    if (verts.empty()) {
        return;
    }
    Allocate(static_cast<int>(verts.size()), verts.data(), GL_STATIC_DRAW);
    _vertexCount = static_cast<int>(verts.size());
}

void LineStripBatch::Stream(const std::vector<RibbonVertex>& verts) {
    const int count = static_cast<int>(verts.size());
    if (!_vao || count > _capacity) {
        Allocate(count, nullptr, GL_DYNAMIC_DRAW);
    }
    if (count > 0) {
        glBindBuffer(GL_ARRAY_BUFFER, _vbo.get());
        glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(count * sizeof(RibbonVertex)), verts.data());
    }
    _vertexCount = count;
}

void LineStripBatch::Clear() {
    _vbo.reset();
    _vao.reset();
    _vertexCount = 0;
    _capacity = 0;
}

void LineStripBatch::Draw() const {
    if (!_vao || _vertexCount <= 0) {
        return;
    }
    glBindVertexArray(_vao.get());
    glDrawArrays(GL_TRIANGLES, 0, _vertexCount);
    glBindVertexArray(0);
}
