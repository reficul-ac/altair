#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace animus::vehicle_core
{

enum class VehicleType
{
    RcPlane,
};

struct VehicleOrientation
{
    float yaw_deg = 0.0F;
    float pitch_deg = 0.0F;
    float roll_deg = 0.0F;
};

struct VehicleDimensions
{
    float length_m = 0.0F;
    float wingspan_m = 0.0F;
    float height_m = 0.0F;
};

struct VehicleDefinition
{
    std::string id;
    std::string display_name;
    VehicleType type = VehicleType::RcPlane;
    std::filesystem::path package_root;
    std::filesystem::path model_path;
    float model_scale = 1.0F;
    VehicleOrientation orientation;
    VehicleDimensions dimensions;
};

enum class VehicleRegistryDiagnosticSeverity
{
    Warning,
    Error,
};

struct VehicleRegistryDiagnostic
{
    VehicleRegistryDiagnosticSeverity severity = VehicleRegistryDiagnosticSeverity::Error;
    std::filesystem::path package_path;
    std::string message;
};

class VehicleRegistry
{
  public:
    static constexpr const char *default_vehicle_id = "animus.rc_plane.generic";

    static VehicleRegistry load_from_directory(const std::filesystem::path &vehicles_root);

    [[nodiscard]] const VehicleDefinition *find(std::string_view id) const;
    [[nodiscard]] const VehicleDefinition *default_definition() const;
    [[nodiscard]] const std::vector<VehicleDefinition> &definitions() const;
    [[nodiscard]] const std::vector<VehicleRegistryDiagnostic> &diagnostics() const;
    [[nodiscard]] std::size_t package_count() const;

  private:
    std::vector<VehicleDefinition> definitions_;
    std::vector<VehicleRegistryDiagnostic> diagnostics_;
    std::size_t package_count_ = 0;
};

[[nodiscard]] std::string_view to_string(VehicleType type);
[[nodiscard]] std::optional<VehicleType> vehicle_type_from_string(std::string_view value);
[[nodiscard]] std::string_view to_string(VehicleRegistryDiagnosticSeverity severity);

} // namespace animus::vehicle_core
