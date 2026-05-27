#include "animus/telemetry_core/telemetry.hpp"

#include "canonical_proto.hpp"
#include "timeline_builder.hpp"

#include <hdf5.h>

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace animus::telemetry_core
{
namespace
{

constexpr const char *canonical_group_path = "/animus/telemetry/v1";
constexpr const char *canonical_samples_path = "/animus/telemetry/v1/samples";
constexpr std::size_t canonical_column_count = 16U;

enum FieldMask : std::uint32_t
{
    Position = 1U << 0U,
    AltitudeMsl = 1U << 1U,
    AltitudeRelative = 1U << 2U,
    Attitude = 1U << 3U,
    Velocity = 1U << 4U,
    Heading = 1U << 5U,
};

struct H5Handle
{
    hid_t id = H5I_INVALID_HID;
    herr_t (*close_fn)(hid_t) = nullptr;

    H5Handle() = default;
    H5Handle(const hid_t value, herr_t (*closer)(hid_t)) : id(value), close_fn(closer)
    {
    }
    H5Handle(const H5Handle &) = delete;
    H5Handle &operator=(const H5Handle &) = delete;
    H5Handle(H5Handle &&other) noexcept : id(other.id), close_fn(other.close_fn)
    {
        other.id = H5I_INVALID_HID;
        other.close_fn = nullptr;
    }
    H5Handle &operator=(H5Handle &&other) noexcept
    {
        if (this != &other)
        {
            reset();
            id = other.id;
            close_fn = other.close_fn;
            other.id = H5I_INVALID_HID;
            other.close_fn = nullptr;
        }
        return *this;
    }
    ~H5Handle()
    {
        reset();
    }
    void reset()
    {
        if (id >= 0 && close_fn != nullptr)
        {
            close_fn(id);
        }
        id = H5I_INVALID_HID;
        close_fn = nullptr;
    }
    [[nodiscard]] bool valid() const
    {
        return id >= 0;
    }
};

bool has_mask(const std::uint32_t mask, const FieldMask flag)
{
    return (mask & static_cast<std::uint32_t>(flag)) != 0U;
}

std::string read_string_attribute(const hid_t object, const char *name)
{
    H5Handle attr(H5Aopen(object, name, H5P_DEFAULT), H5Aclose);
    if (!attr.valid())
    {
        return {};
    }
    H5Handle type(H5Aget_type(attr.id), H5Tclose);
    if (!type.valid() || H5Tget_class(type.id) != H5T_STRING)
    {
        return {};
    }
    if (H5Tis_variable_str(type.id) > 0)
    {
        char *value = nullptr;
        if (H5Aread(attr.id, type.id, &value) < 0 || value == nullptr)
        {
            return {};
        }
        std::string result(value);
        H5free_memory(value);
        return result;
    }
    const std::size_t size = H5Tget_size(type.id);
    std::vector<char> bytes(size + 1U, '\0');
    if (H5Aread(attr.id, type.id, bytes.data()) < 0)
    {
        return {};
    }
    return std::string(bytes.data());
}

bool read_integer_attribute(const hid_t object, const char *name, int &value)
{
    H5Handle attr(H5Aopen(object, name, H5P_DEFAULT), H5Aclose);
    if (!attr.valid())
    {
        return false;
    }
    return H5Aread(attr.id, H5T_NATIVE_INT, &value) >= 0;
}

AltitudeDatum datum_from_double(const double value)
{
    switch (static_cast<int>(value))
    {
    case 1:
        return AltitudeDatum::MslOrthometric;
    case 2:
        return AltitudeDatum::Ellipsoid;
    case 3:
        return AltitudeDatum::TerrainRelative;
    default:
        return AltitudeDatum::Unknown;
    }
}

} // namespace

Timeline load_hdf5(const std::filesystem::path &path)
{
    Timeline timeline;
    timeline.source_format = TelemetryImportFormat::Hdf5Animus;

    H5Handle file(H5Fopen(path.string().c_str(), H5F_ACC_RDONLY, H5P_DEFAULT), H5Fclose);
    if (!file.valid())
    {
        throw std::runtime_error("Failed to open telemetry HDF5: " + path.string());
    }

    H5Handle group(H5Gopen2(file.id, canonical_group_path, H5P_DEFAULT), H5Gclose);
    H5Handle dataset(H5Dopen2(file.id, canonical_samples_path, H5P_DEFAULT), H5Dclose);
    if (!group.valid() || !dataset.valid())
    {
        ++timeline.diagnostics.unsupported_layouts;
        add_event(timeline, 0.0, {}, 0U, EventSeverity::Error, "Unsupported HDF5 telemetry layout");
        finalize_timeline(timeline);
        return timeline;
    }

    int version = 0;
    const std::string schema = read_string_attribute(group.id, "schema");
    if (schema != canonical_protobuf_schema_name() ||
        !read_integer_attribute(group.id, "version", version) || version != 1)
    {
        ++timeline.diagnostics.schema_mismatches;
        add_event(timeline, 0.0, {}, 0U, EventSeverity::Error, "Unsupported HDF5 telemetry schema");
        finalize_timeline(timeline);
        return timeline;
    }

    H5Handle space(H5Dget_space(dataset.id), H5Sclose);
    if (!space.valid() || H5Sget_simple_extent_ndims(space.id) != 2)
    {
        ++timeline.diagnostics.unsupported_layouts;
        add_event(timeline, 0.0, {}, 0U, EventSeverity::Error, "HDF5 samples must be a 2D table");
        finalize_timeline(timeline);
        return timeline;
    }
    hsize_t dims[2] = {0U, 0U};
    H5Sget_simple_extent_dims(space.id, dims, nullptr);
    if (dims[1] != canonical_column_count)
    {
        ++timeline.diagnostics.unsupported_layouts;
        add_event(timeline,
                  0.0,
                  {},
                  0U,
                  EventSeverity::Error,
                  "HDF5 samples table has unsupported column count");
        finalize_timeline(timeline);
        return timeline;
    }

    std::vector<double> values(static_cast<std::size_t>(dims[0] * dims[1]), 0.0);
    if (!values.empty() &&
        H5Dread(dataset.id, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT, values.data()) < 0)
    {
        ++timeline.diagnostics.decode_failures;
        add_event(timeline, 0.0, {}, 0U, EventSeverity::Error, "Failed to read HDF5 samples");
        finalize_timeline(timeline);
        return timeline;
    }

    for (std::size_t row = 0U; row < static_cast<std::size_t>(dims[0]); ++row)
    {
        const double *sample_values = values.data() + row * canonical_column_count;
        const std::uint32_t mask = static_cast<std::uint32_t>(sample_values[14]);
        const bool timestamp_valid = sample_values[15] != 0.0;
        if (!timestamp_valid || !has_mask(mask, FieldMask::Position))
        {
            ++timeline.diagnostics.missing_required_fields;
            ++timeline.diagnostics.skipped_records;
            continue;
        }
        const int system_id = static_cast<int>(sample_values[1]);
        const int component_id = static_cast<int>(sample_values[2]);
        if (system_id < 0 || system_id > 255 || component_id < 0 || component_id > 255)
        {
            ++timeline.diagnostics.decode_failures;
            ++timeline.diagnostics.skipped_records;
            continue;
        }

        TelemetrySample sample;
        sample.time_s = sample_values[0];
        sample.entity_id.system_id = static_cast<std::uint8_t>(system_id);
        sample.entity_id.component_id = static_cast<std::uint8_t>(component_id);
        sample.lat_deg = sample_values[3];
        sample.lon_deg = sample_values[4];
        sample.altitude_datum = datum_from_double(sample_values[13]);
        sample.fields.position = true;
        sample.fields.altitude_msl = has_mask(mask, FieldMask::AltitudeMsl);
        sample.fields.altitude_relative = has_mask(mask, FieldMask::AltitudeRelative);
        sample.fields.attitude = has_mask(mask, FieldMask::Attitude);
        sample.fields.velocity = has_mask(mask, FieldMask::Velocity);
        sample.fields.heading = has_mask(mask, FieldMask::Heading);
        if (sample.fields.altitude_msl)
        {
            sample.altitude_msl_m = sample_values[5];
        }
        if (sample.fields.altitude_relative)
        {
            sample.altitude_relative_m = sample_values[6];
        }
        if (sample.fields.attitude)
        {
            sample.roll_rad = sample_values[7];
            sample.pitch_rad = sample_values[8];
            sample.yaw_rad = sample_values[9];
        }
        if (sample.fields.velocity)
        {
            sample.ground_speed_mps = sample_values[10];
            sample.climb_rate_mps = sample_values[11];
        }
        if (sample.fields.heading)
        {
            sample.heading_deg = sample_values[12];
        }
        timeline.samples.push_back(sample);
    }

    finalize_timeline(timeline);
    return timeline;
}

} // namespace animus::telemetry_core
