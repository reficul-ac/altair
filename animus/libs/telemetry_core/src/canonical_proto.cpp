#include "animus/telemetry_core/telemetry.hpp"

#include "animus_telemetry_v1.pb.h"

#include <limits>
#include <optional>
#include <span>
#include <string>

namespace animus::telemetry_core
{
namespace
{

constexpr const char *canonical_schema_name = "animus.telemetry.v1.TelemetrySample";

AltitudeDatum convert_datum(const animus::telemetry::v1::AltitudeDatum datum)
{
    switch (datum)
    {
    case animus::telemetry::v1::ALTITUDE_DATUM_MSL_ORTHOMETRIC:
        return AltitudeDatum::MslOrthometric;
    case animus::telemetry::v1::ALTITUDE_DATUM_ELLIPSOID:
        return AltitudeDatum::Ellipsoid;
    case animus::telemetry::v1::ALTITUDE_DATUM_TERRAIN_RELATIVE:
        return AltitudeDatum::TerrainRelative;
    case animus::telemetry::v1::ALTITUDE_DATUM_UNKNOWN:
    default:
        return AltitudeDatum::Unknown;
    }
}

bool valid_entity_id(const std::uint32_t value)
{
    return value <= static_cast<std::uint32_t>(std::numeric_limits<std::uint8_t>::max());
}

} // namespace

bool decode_canonical_sample_protobuf(std::span<const std::byte> bytes,
                                      TelemetrySample &sample,
                                      ParserDiagnostics &diagnostics)
{
    animus::telemetry::v1::TelemetrySample message;
    if (!message.ParseFromArray(bytes.data(), static_cast<int>(bytes.size())))
    {
        ++diagnostics.decode_failures;
        return false;
    }
    if (!message.timestamp_valid() || !message.fields().position())
    {
        ++diagnostics.missing_required_fields;
        ++diagnostics.skipped_records;
        return false;
    }
    if (!valid_entity_id(message.system_id()) || !valid_entity_id(message.component_id()))
    {
        ++diagnostics.decode_failures;
        ++diagnostics.skipped_records;
        return false;
    }

    sample = {};
    sample.time_s = message.timestamp_s();
    sample.entity_id.system_id = static_cast<std::uint8_t>(message.system_id());
    sample.entity_id.component_id = static_cast<std::uint8_t>(message.component_id());
    sample.lat_deg = message.lat_deg();
    sample.lon_deg = message.lon_deg();
    sample.altitude_datum = convert_datum(message.altitude_datum());
    sample.fields.position = message.fields().position();
    sample.fields.altitude_msl = message.fields().altitude_msl();
    sample.fields.altitude_relative = message.fields().altitude_relative();
    sample.fields.attitude = message.fields().attitude();
    sample.fields.velocity = message.fields().velocity();
    sample.fields.heading = message.fields().heading();
    if (sample.fields.altitude_msl)
    {
        sample.altitude_msl_m = message.altitude_msl_m();
    }
    if (sample.fields.altitude_relative)
    {
        sample.altitude_relative_m = message.altitude_relative_m();
    }
    if (sample.fields.attitude)
    {
        sample.roll_rad = message.roll_rad();
        sample.pitch_rad = message.pitch_rad();
        sample.yaw_rad = message.yaw_rad();
    }
    if (sample.fields.velocity)
    {
        sample.ground_speed_mps = message.ground_speed_mps();
        sample.climb_rate_mps = message.climb_rate_mps();
    }
    if (sample.fields.heading)
    {
        sample.heading_deg = message.heading_deg();
    }
    return true;
}

const char *canonical_protobuf_schema_name()
{
    return canonical_schema_name;
}

} // namespace animus::telemetry_core
