#include "animus/vehicle_core/vehicle_definition.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

namespace animus::vehicle_core
{
namespace
{

std::string trim(std::string_view value)
{
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos)
    {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return std::string(value.substr(first, last - first + 1U));
}

std::string unquote(const std::string &value)
{
    if (value.size() >= 2U && ((value.front() == '"' && value.back() == '"') ||
                               (value.front() == '\'' && value.back() == '\'')))
    {
        return value.substr(1U, value.size() - 2U);
    }
    return value;
}

bool parse_float(std::string_view text, float &out)
{
    const std::string value = trim(text);
    const auto *begin = value.data();
    const auto *end = value.data() + value.size();
    const auto result = std::from_chars(begin, end, out);
    return result.ec == std::errc{} && result.ptr == end && std::isfinite(out);
}

void add_diagnostic(std::vector<VehicleRegistryDiagnostic> &diagnostics,
                    const VehicleRegistryDiagnosticSeverity severity,
                    const std::filesystem::path &package_path,
                    std::string message)
{
    diagnostics.push_back({severity, package_path, std::move(message)});
}

std::map<std::string, std::string> parse_descriptor_fields(const std::filesystem::path &path)
{
    std::ifstream input(path);
    if (!input)
    {
        throw std::runtime_error("failed to open descriptor");
    }

    std::map<std::string, std::string> fields;
    std::string section;
    std::string line;
    while (std::getline(input, line))
    {
        const auto comment = line.find('#');
        if (comment != std::string::npos)
        {
            line.erase(comment);
        }
        if (trim(line).empty())
        {
            continue;
        }
        const bool indented = !line.empty() && (line.front() == ' ' || line.front() == '\t');
        const std::string stripped = trim(line);
        const auto colon = stripped.find(':');
        if (colon == std::string::npos)
        {
            continue;
        }
        const std::string key = trim(std::string_view(stripped).substr(0U, colon));
        const std::string value = trim(std::string_view(stripped).substr(colon + 1U));
        if (!indented && value.empty())
        {
            section = key;
            continue;
        }
        const std::string full_key = indented && !section.empty() ? section + "." + key : key;
        fields[full_key] = unquote(value);
        if (!indented)
        {
            section.clear();
        }
    }
    return fields;
}

std::optional<std::string> required_string(const std::map<std::string, std::string> &fields,
                                           const std::string &key,
                                           std::vector<VehicleRegistryDiagnostic> &diagnostics,
                                           const std::filesystem::path &descriptor)
{
    const auto it = fields.find(key);
    if (it == fields.end() || trim(it->second).empty())
    {
        add_diagnostic(diagnostics,
                       VehicleRegistryDiagnosticSeverity::Error,
                       descriptor,
                       "missing required field '" + key + "'");
        return std::nullopt;
    }
    return it->second;
}

std::optional<float> required_float(const std::map<std::string, std::string> &fields,
                                    const std::string &key,
                                    std::vector<VehicleRegistryDiagnostic> &diagnostics,
                                    const std::filesystem::path &descriptor)
{
    const auto text = required_string(fields, key, diagnostics, descriptor);
    if (!text)
    {
        return std::nullopt;
    }
    float value = 0.0F;
    if (!parse_float(*text, value))
    {
        add_diagnostic(diagnostics,
                       VehicleRegistryDiagnosticSeverity::Error,
                       descriptor,
                       "field '" + key + "' must be a finite number");
        return std::nullopt;
    }
    return value;
}

std::optional<VehicleDefinition>
load_descriptor(const std::filesystem::path &descriptor,
                std::vector<VehicleRegistryDiagnostic> &diagnostics)
{
    std::map<std::string, std::string> fields;
    try
    {
        fields = parse_descriptor_fields(descriptor);
    }
    catch (const std::exception &error)
    {
        add_diagnostic(diagnostics,
                       VehicleRegistryDiagnosticSeverity::Error,
                       descriptor,
                       std::string("descriptor parse failed: ") + error.what());
        return std::nullopt;
    }

    const auto id = required_string(fields, "id", diagnostics, descriptor);
    const auto display_name = required_string(fields, "display_name", diagnostics, descriptor);
    const auto type_text = required_string(fields, "type", diagnostics, descriptor);
    const auto model_path_text = required_string(fields, "model.path", diagnostics, descriptor);
    const auto model_scale = required_float(fields, "model.scale", diagnostics, descriptor);
    const auto yaw = required_float(fields, "orientation.yaw_deg", diagnostics, descriptor);
    const auto pitch = required_float(fields, "orientation.pitch_deg", diagnostics, descriptor);
    const auto roll = required_float(fields, "orientation.roll_deg", diagnostics, descriptor);
    const auto length = required_float(fields, "dimensions.length_m", diagnostics, descriptor);
    const auto wingspan = required_float(fields, "dimensions.wingspan_m", diagnostics, descriptor);
    const auto height = required_float(fields, "dimensions.height_m", diagnostics, descriptor);
    if (!id || !display_name || !type_text || !model_path_text || !model_scale || !yaw || !pitch ||
        !roll || !length || !wingspan || !height)
    {
        return std::nullopt;
    }

    const auto type = vehicle_type_from_string(*type_text);
    if (!type)
    {
        add_diagnostic(diagnostics,
                       VehicleRegistryDiagnosticSeverity::Error,
                       descriptor,
                       "unknown vehicle type '" + *type_text + "'");
        return std::nullopt;
    }

    const std::filesystem::path package_root = descriptor.parent_path();
    const std::filesystem::path model_path = package_root / *model_path_text;
    if (model_path.extension() != ".glb")
    {
        add_diagnostic(diagnostics,
                       VehicleRegistryDiagnosticSeverity::Error,
                       descriptor,
                       "model.path must reference a .glb file");
        return std::nullopt;
    }
    if (*model_scale <= 0.0F)
    {
        add_diagnostic(diagnostics,
                       VehicleRegistryDiagnosticSeverity::Error,
                       descriptor,
                       "model.scale must be greater than zero");
        return std::nullopt;
    }
    if (!std::filesystem::exists(model_path))
    {
        add_diagnostic(diagnostics,
                       VehicleRegistryDiagnosticSeverity::Warning,
                       descriptor,
                       "model file is missing: " + model_path.string());
    }

    return VehicleDefinition{
        *id,
        *display_name,
        *type,
        package_root,
        model_path,
        *model_scale,
        {*yaw, *pitch, *roll},
        {*length, *wingspan, *height},
    };
}

} // namespace

std::string_view to_string(const VehicleType type)
{
    switch (type)
    {
    case VehicleType::RcPlane:
        return "rc_plane";
    }
    return "unknown";
}

std::optional<VehicleType> vehicle_type_from_string(const std::string_view value)
{
    if (value == "rc_plane")
    {
        return VehicleType::RcPlane;
    }
    return std::nullopt;
}

std::string_view to_string(const VehicleRegistryDiagnosticSeverity severity)
{
    switch (severity)
    {
    case VehicleRegistryDiagnosticSeverity::Warning:
        return "warning";
    case VehicleRegistryDiagnosticSeverity::Error:
        return "error";
    }
    return "error";
}

VehicleRegistry VehicleRegistry::load_from_directory(const std::filesystem::path &vehicles_root)
{
    VehicleRegistry registry;
    if (!std::filesystem::exists(vehicles_root))
    {
        add_diagnostic(registry.diagnostics_,
                       VehicleRegistryDiagnosticSeverity::Error,
                       vehicles_root,
                       "vehicle package root does not exist");
        return registry;
    }

    std::set<std::string> ids;
    std::vector<std::filesystem::path> descriptors;
    for (const auto &entry : std::filesystem::directory_iterator(vehicles_root))
    {
        if (!entry.is_directory())
        {
            continue;
        }
        const std::filesystem::path descriptor = entry.path() / "vehicle.animus.yaml";
        if (std::filesystem::exists(descriptor))
        {
            descriptors.push_back(descriptor);
        }
    }
    std::sort(descriptors.begin(), descriptors.end());

    for (const auto &descriptor : descriptors)
    {
        ++registry.package_count_;
        auto definition = load_descriptor(descriptor, registry.diagnostics_);
        if (!definition)
        {
            continue;
        }
        if (!ids.insert(definition->id).second)
        {
            add_diagnostic(registry.diagnostics_,
                           VehicleRegistryDiagnosticSeverity::Error,
                           descriptor,
                           "duplicate vehicle id '" + definition->id + "'");
            continue;
        }
        registry.definitions_.push_back(std::move(*definition));
    }
    return registry;
}

const VehicleDefinition *VehicleRegistry::find(const std::string_view id) const
{
    const auto it =
        std::find_if(definitions_.begin(),
                     definitions_.end(),
                     [id](const VehicleDefinition &definition) { return definition.id == id; });
    return it == definitions_.end() ? nullptr : &*it;
}

const VehicleDefinition *VehicleRegistry::default_definition() const
{
    return find(default_vehicle_id);
}

const std::vector<VehicleDefinition> &VehicleRegistry::definitions() const
{
    return definitions_;
}

const std::vector<VehicleRegistryDiagnostic> &VehicleRegistry::diagnostics() const
{
    return diagnostics_;
}

std::size_t VehicleRegistry::package_count() const
{
    return package_count_;
}

} // namespace animus::vehicle_core
