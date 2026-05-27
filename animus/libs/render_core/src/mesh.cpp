#include "animus/render_core/mesh.hpp"

#include <GL/glew.h>

#include <array>
#include <cstddef>
#include <stdexcept>
#include <utility>

namespace animus::render_core
{

TriangleMesh::TriangleMesh()
{
    constexpr std::array<float, 15> vertices = {
        0.0F,
        0.55F,
        0.0F,
        0.95F,
        0.35F,
        -0.65F,
        -0.55F,
        0.0F,
        0.15F,
        0.70F,
        0.65F,
        -0.55F,
        0.0F,
        0.20F,
        0.45F,
    };

    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);

    glGenBuffers(1, &vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertices.size() * sizeof(float)),
                 vertices.data(),
                 GL_STATIC_DRAW);

    constexpr GLsizei stride = 5 * sizeof(float);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(
        1, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void *>(3 * sizeof(float)));

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    vertex_count_ = 3;
}

TriangleMesh::~TriangleMesh()
{
    if (vbo_ != 0)
    {
        glDeleteBuffers(1, &vbo_);
    }
    if (vao_ != 0)
    {
        glDeleteVertexArrays(1, &vao_);
    }
}

TriangleMesh::TriangleMesh(TriangleMesh &&other) noexcept
    : vao_(std::exchange(other.vao_, 0)), vbo_(std::exchange(other.vbo_, 0)),
      vertex_count_(std::exchange(other.vertex_count_, 0))
{
}

TriangleMesh &TriangleMesh::operator=(TriangleMesh &&other) noexcept
{
    if (this != &other)
    {
        if (vbo_ != 0)
        {
            glDeleteBuffers(1, &vbo_);
        }
        if (vao_ != 0)
        {
            glDeleteVertexArrays(1, &vao_);
        }
        vao_ = std::exchange(other.vao_, 0);
        vbo_ = std::exchange(other.vbo_, 0);
        vertex_count_ = std::exchange(other.vertex_count_, 0);
    }
    return *this;
}

void TriangleMesh::draw() const
{
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, vertex_count_);
    glBindVertexArray(0);
}

IndexedMesh::IndexedMesh(std::span<const TerrainVertex> vertices,
                         std::span<const std::uint32_t> indices)
{
    if (vertices.empty() || indices.empty())
    {
        throw std::invalid_argument("IndexedMesh requires vertices and indices");
    }

    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);

    glGenBuffers(1, &vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertices.size_bytes()),
                 vertices.data(),
                 GL_STATIC_DRAW);

    glGenBuffers(1, &ebo_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(indices.size_bytes()),
                 indices.data(),
                 GL_STATIC_DRAW);

    constexpr GLsizei stride = sizeof(TerrainVertex);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(
        1, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void *>(offsetof(TerrainVertex, u)));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    index_count_ = indices.size();
}

IndexedMesh::~IndexedMesh()
{
    if (ebo_ != 0)
    {
        glDeleteBuffers(1, &ebo_);
    }
    if (vbo_ != 0)
    {
        glDeleteBuffers(1, &vbo_);
    }
    if (vao_ != 0)
    {
        glDeleteVertexArrays(1, &vao_);
    }
}

IndexedMesh::IndexedMesh(IndexedMesh &&other) noexcept
    : vao_(std::exchange(other.vao_, 0)), vbo_(std::exchange(other.vbo_, 0)),
      ebo_(std::exchange(other.ebo_, 0)), index_count_(std::exchange(other.index_count_, 0))
{
}

IndexedMesh &IndexedMesh::operator=(IndexedMesh &&other) noexcept
{
    if (this != &other)
    {
        if (ebo_ != 0)
        {
            glDeleteBuffers(1, &ebo_);
        }
        if (vbo_ != 0)
        {
            glDeleteBuffers(1, &vbo_);
        }
        if (vao_ != 0)
        {
            glDeleteVertexArrays(1, &vao_);
        }
        vao_ = std::exchange(other.vao_, 0);
        vbo_ = std::exchange(other.vbo_, 0);
        ebo_ = std::exchange(other.ebo_, 0);
        index_count_ = std::exchange(other.index_count_, 0);
    }
    return *this;
}

void IndexedMesh::draw() const
{
    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(index_count_), GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

std::size_t IndexedMesh::index_count() const
{
    return index_count_;
}

ModelPrimitiveMesh::ModelPrimitiveMesh(std::span<const ModelVertex> vertices,
                                       std::span<const std::uint32_t> indices,
                                       std::array<float, 4> base_color)
    : base_color_(base_color)
{
    if (vertices.empty() || indices.empty())
    {
        throw std::invalid_argument("ModelPrimitiveMesh requires vertices and indices");
    }

    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);

    glGenBuffers(1, &vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertices.size_bytes()),
                 vertices.data(),
                 GL_STATIC_DRAW);

    glGenBuffers(1, &ebo_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(indices.size_bytes()),
                 indices.data(),
                 GL_STATIC_DRAW);

    constexpr GLsizei stride = sizeof(ModelVertex);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(
        1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void *>(offsetof(ModelVertex, nx)));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    index_count_ = indices.size();
}

ModelPrimitiveMesh::~ModelPrimitiveMesh()
{
    if (ebo_ != 0)
    {
        glDeleteBuffers(1, &ebo_);
    }
    if (vbo_ != 0)
    {
        glDeleteBuffers(1, &vbo_);
    }
    if (vao_ != 0)
    {
        glDeleteVertexArrays(1, &vao_);
    }
}

ModelPrimitiveMesh::ModelPrimitiveMesh(ModelPrimitiveMesh &&other) noexcept
    : vao_(std::exchange(other.vao_, 0)), vbo_(std::exchange(other.vbo_, 0)),
      ebo_(std::exchange(other.ebo_, 0)), index_count_(std::exchange(other.index_count_, 0)),
      base_color_(std::exchange(other.base_color_, std::array<float, 4>{1.0F, 1.0F, 1.0F, 1.0F}))
{
}

ModelPrimitiveMesh &ModelPrimitiveMesh::operator=(ModelPrimitiveMesh &&other) noexcept
{
    if (this != &other)
    {
        if (ebo_ != 0)
        {
            glDeleteBuffers(1, &ebo_);
        }
        if (vbo_ != 0)
        {
            glDeleteBuffers(1, &vbo_);
        }
        if (vao_ != 0)
        {
            glDeleteVertexArrays(1, &vao_);
        }
        vao_ = std::exchange(other.vao_, 0);
        vbo_ = std::exchange(other.vbo_, 0);
        ebo_ = std::exchange(other.ebo_, 0);
        index_count_ = std::exchange(other.index_count_, 0);
        base_color_ =
            std::exchange(other.base_color_, std::array<float, 4>{1.0F, 1.0F, 1.0F, 1.0F});
    }
    return *this;
}

void ModelPrimitiveMesh::draw() const
{
    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(index_count_), GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

const std::array<float, 4> &ModelPrimitiveMesh::base_color() const
{
    return base_color_;
}

ModelMesh::ModelMesh(std::span<const ModelPrimitive> primitives)
{
    if (primitives.empty())
    {
        throw std::invalid_argument("ModelMesh requires at least one primitive");
    }
    primitives_.reserve(primitives.size());
    for (const auto &primitive : primitives)
    {
        primitives_.emplace_back(primitive.vertices, primitive.indices, primitive.base_color);
    }
}

const std::vector<ModelPrimitiveMesh> &ModelMesh::primitives() const
{
    return primitives_;
}

} // namespace animus::render_core
