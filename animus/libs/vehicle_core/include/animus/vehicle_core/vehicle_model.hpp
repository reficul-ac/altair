#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace animus::vehicle_core
{

struct VehicleModelVertex
{
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float nx = 0.0F;
    float ny = 1.0F;
    float nz = 0.0F;
};

struct VehicleModelPrimitive
{
    std::vector<VehicleModelVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::array<float, 4> base_color{0.78F, 0.82F, 0.86F, 1.0F};
};

struct VehicleModelCpu
{
    std::vector<VehicleModelPrimitive> primitives;
};

[[nodiscard]] VehicleModelCpu load_glb_model(const std::filesystem::path &path);

} // namespace animus::vehicle_core
