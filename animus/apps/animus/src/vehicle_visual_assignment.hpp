#pragma once

#include <map>
#include <string>

namespace animus::app
{

struct VehicleVisualAssignment
{
    std::string vehicle_id = "animus.rc_plane.generic";
    bool force_icon_only = false;
    float scale = 1.0F;
    std::string heading_source = "auto";
    std::string altitude_placement = "terrain_resolved";

    bool operator==(const VehicleVisualAssignment &) const = default;
};

struct VehicleVisualAssignments
{
    std::map<std::string, std::string> defaults_by_type;
    std::map<std::string, VehicleVisualAssignment> entities;

    bool operator==(const VehicleVisualAssignments &) const = default;
};

} // namespace animus::app
