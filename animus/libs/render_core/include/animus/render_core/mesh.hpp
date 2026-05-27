#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace animus::render_core
{

class TriangleMesh
{
  public:
    TriangleMesh();
    ~TriangleMesh();

    TriangleMesh(const TriangleMesh &) = delete;
    TriangleMesh &operator=(const TriangleMesh &) = delete;
    TriangleMesh(TriangleMesh &&other) noexcept;
    TriangleMesh &operator=(TriangleMesh &&other) noexcept;

    void draw() const;

  private:
    unsigned int vao_ = 0;
    unsigned int vbo_ = 0;
    int vertex_count_ = 0;
};

struct TerrainVertex
{
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float u = 0.0F;
    float v = 0.0F;
};

class IndexedMesh
{
  public:
    IndexedMesh(std::span<const TerrainVertex> vertices, std::span<const std::uint32_t> indices);
    ~IndexedMesh();

    IndexedMesh(const IndexedMesh &) = delete;
    IndexedMesh &operator=(const IndexedMesh &) = delete;
    IndexedMesh(IndexedMesh &&other) noexcept;
    IndexedMesh &operator=(IndexedMesh &&other) noexcept;

    void draw() const;
    [[nodiscard]] std::size_t index_count() const;

  private:
    unsigned int vao_ = 0;
    unsigned int vbo_ = 0;
    unsigned int ebo_ = 0;
    std::size_t index_count_ = 0;
};

struct ModelVertex
{
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float nx = 0.0F;
    float ny = 1.0F;
    float nz = 0.0F;
};

struct ModelPrimitive
{
    std::vector<ModelVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::array<float, 4> base_color{1.0F, 1.0F, 1.0F, 1.0F};
};

class ModelPrimitiveMesh
{
  public:
    ModelPrimitiveMesh(std::span<const ModelVertex> vertices,
                       std::span<const std::uint32_t> indices,
                       std::array<float, 4> base_color);
    ~ModelPrimitiveMesh();

    ModelPrimitiveMesh(const ModelPrimitiveMesh &) = delete;
    ModelPrimitiveMesh &operator=(const ModelPrimitiveMesh &) = delete;
    ModelPrimitiveMesh(ModelPrimitiveMesh &&other) noexcept;
    ModelPrimitiveMesh &operator=(ModelPrimitiveMesh &&other) noexcept;

    void draw() const;
    [[nodiscard]] const std::array<float, 4> &base_color() const;

  private:
    unsigned int vao_ = 0;
    unsigned int vbo_ = 0;
    unsigned int ebo_ = 0;
    std::size_t index_count_ = 0;
    std::array<float, 4> base_color_{1.0F, 1.0F, 1.0F, 1.0F};
};

class ModelMesh
{
  public:
    explicit ModelMesh(std::span<const ModelPrimitive> primitives);

    [[nodiscard]] const std::vector<ModelPrimitiveMesh> &primitives() const;

  private:
    std::vector<ModelPrimitiveMesh> primitives_;
};

} // namespace animus::render_core
