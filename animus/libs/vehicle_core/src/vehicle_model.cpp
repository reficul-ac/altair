#include "animus/vehicle_core/vehicle_model.hpp"

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace animus::vehicle_core
{
namespace
{

struct CgltfDeleter
{
    void operator()(cgltf_data *data) const
    {
        cgltf_free(data);
    }
};

using CgltfDataPtr = std::unique_ptr<cgltf_data, CgltfDeleter>;

std::string cgltf_error(const cgltf_result result)
{
    switch (result)
    {
    case cgltf_result_success:
        return "success";
    case cgltf_result_data_too_short:
        return "data too short";
    case cgltf_result_unknown_format:
        return "unknown format";
    case cgltf_result_invalid_json:
        return "invalid json";
    case cgltf_result_invalid_gltf:
        return "invalid gltf";
    case cgltf_result_invalid_options:
        return "invalid options";
    case cgltf_result_file_not_found:
        return "file not found";
    case cgltf_result_io_error:
        return "io error";
    case cgltf_result_out_of_memory:
        return "out of memory";
    case cgltf_result_legacy_gltf:
        return "legacy gltf";
    case cgltf_result_max_enum:
        return "unknown error";
    }
    return "unknown error";
}

std::array<float, 3> transform_position(const float *matrix, const std::array<float, 3> &value)
{
    return {
        matrix[0] * value[0] + matrix[4] * value[1] + matrix[8] * value[2] + matrix[12],
        matrix[1] * value[0] + matrix[5] * value[1] + matrix[9] * value[2] + matrix[13],
        matrix[2] * value[0] + matrix[6] * value[1] + matrix[10] * value[2] + matrix[14],
    };
}

std::array<float, 3> transform_normal(const float *matrix, const std::array<float, 3> &value)
{
    return {
        matrix[0] * value[0] + matrix[4] * value[1] + matrix[8] * value[2],
        matrix[1] * value[0] + matrix[5] * value[1] + matrix[9] * value[2],
        matrix[2] * value[0] + matrix[6] * value[1] + matrix[10] * value[2],
    };
}

std::array<float, 4> primitive_color(const cgltf_primitive &primitive)
{
    if (primitive.material == nullptr)
    {
        return {0.78F, 0.82F, 0.86F, 1.0F};
    }
    const auto &color = primitive.material->pbr_metallic_roughness.base_color_factor;
    return {color[0], color[1], color[2], color[3]};
}

void append_node_primitives(const cgltf_node &node, VehicleModelCpu &model)
{
    if (node.mesh == nullptr)
    {
        return;
    }

    float world[16]{};
    cgltf_node_transform_world(&node, world);
    for (cgltf_size primitive_index = 0; primitive_index < node.mesh->primitives_count;
         ++primitive_index)
    {
        const cgltf_primitive &primitive = node.mesh->primitives[primitive_index];
        if (primitive.type != cgltf_primitive_type_triangles)
        {
            continue;
        }
        const cgltf_accessor *positions =
            cgltf_find_accessor(&primitive, cgltf_attribute_type_position, 0);
        if (positions == nullptr || positions->count == 0U)
        {
            continue;
        }
        const cgltf_accessor *normals =
            cgltf_find_accessor(&primitive, cgltf_attribute_type_normal, 0);

        VehicleModelPrimitive out;
        out.base_color = primitive_color(primitive);
        out.vertices.reserve(static_cast<std::size_t>(positions->count));
        for (cgltf_size vertex_index = 0; vertex_index < positions->count; ++vertex_index)
        {
            cgltf_float position_values[3]{};
            if (!cgltf_accessor_read_float(positions, vertex_index, position_values, 3))
            {
                throw std::runtime_error("failed to read GLB position accessor");
            }
            std::array<float, 3> normal_values{0.0F, 1.0F, 0.0F};
            if (normals != nullptr)
            {
                cgltf_float read_normal[3]{};
                if (cgltf_accessor_read_float(normals, vertex_index, read_normal, 3))
                {
                    normal_values = {read_normal[0], read_normal[1], read_normal[2]};
                }
            }
            const auto p = transform_position(
                world, {position_values[0], position_values[1], position_values[2]});
            const auto n = transform_normal(world, normal_values);
            out.vertices.push_back({p[0], p[1], p[2], n[0], n[1], n[2]});
        }

        if (primitive.indices != nullptr)
        {
            out.indices.reserve(static_cast<std::size_t>(primitive.indices->count));
            for (cgltf_size index = 0; index < primitive.indices->count; ++index)
            {
                out.indices.push_back(static_cast<std::uint32_t>(
                    cgltf_accessor_read_index(primitive.indices, index)));
            }
        }
        else
        {
            out.indices.reserve(out.vertices.size());
            for (std::uint32_t index = 0; index < out.vertices.size(); ++index)
            {
                out.indices.push_back(index);
            }
        }

        if (!out.vertices.empty() && !out.indices.empty())
        {
            model.primitives.push_back(std::move(out));
        }
    }
}

} // namespace

VehicleModelCpu load_glb_model(const std::filesystem::path &path)
{
    cgltf_options options{};
    cgltf_data *raw_data = nullptr;
    const cgltf_result parse_result = cgltf_parse_file(&options, path.string().c_str(), &raw_data);
    if (parse_result != cgltf_result_success)
    {
        throw std::runtime_error("GLB parse failed for " + path.string() + ": " +
                                 cgltf_error(parse_result));
    }
    CgltfDataPtr data(raw_data);
    const cgltf_result buffer_result =
        cgltf_load_buffers(&options, data.get(), path.string().c_str());
    if (buffer_result != cgltf_result_success)
    {
        throw std::runtime_error("GLB buffer load failed for " + path.string() + ": " +
                                 cgltf_error(buffer_result));
    }
    const cgltf_result validate_result = cgltf_validate(data.get());
    if (validate_result != cgltf_result_success)
    {
        throw std::runtime_error("GLB validation failed for " + path.string() + ": " +
                                 cgltf_error(validate_result));
    }

    VehicleModelCpu model;
    if (data->scene != nullptr)
    {
        for (cgltf_size node_index = 0; node_index < data->scene->nodes_count; ++node_index)
        {
            append_node_primitives(*data->scene->nodes[node_index], model);
        }
    }
    else
    {
        for (cgltf_size node_index = 0; node_index < data->nodes_count; ++node_index)
        {
            append_node_primitives(data->nodes[node_index], model);
        }
    }

    if (model.primitives.empty())
    {
        throw std::runtime_error("GLB contains no supported triangle mesh primitives: " +
                                 path.string());
    }
    return model;
}

} // namespace animus::vehicle_core
