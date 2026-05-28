#include "animus/telemetry_core/mavlink.hpp"

#include <array>
#include <algorithm>
#include <cstddef>
#include <cstring>

namespace animus::telemetry_core
{
namespace
{

constexpr std::uint8_t mavlink_v1_magic = 0xFE;
constexpr std::uint8_t mavlink_v2_magic = 0xFD;
constexpr std::uint8_t mavlink_v2_signed_flag = 0x01;

void crc_accumulate(std::uint8_t byte, std::uint16_t &crc)
{
    byte ^= static_cast<std::uint8_t>(crc & 0xFFU);
    byte ^= static_cast<std::uint8_t>(byte << 4U);
    crc = static_cast<std::uint16_t>((crc >> 8U) ^ (static_cast<std::uint16_t>(byte) << 8U) ^
                                     (static_cast<std::uint16_t>(byte) << 3U) ^
                                     (static_cast<std::uint16_t>(byte) >> 4U));
}

std::uint16_t little_u16(const std::span<const std::uint8_t> bytes, const std::size_t offset)
{
    return static_cast<std::uint16_t>(bytes[offset]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U);
}

std::uint32_t little_u32(const std::span<const std::uint8_t> bytes, const std::size_t offset)
{
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
           (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

std::uint64_t little_u64(const std::span<const std::uint8_t> bytes, const std::size_t offset)
{
    std::uint64_t value = 0U;
    for (std::size_t index = 0U; index < 8U; ++index)
    {
        value |= static_cast<std::uint64_t>(bytes[offset + index]) << (index * 8U);
    }
    return value;
}

std::int16_t little_i16(const std::span<const std::uint8_t> bytes, const std::size_t offset)
{
    return static_cast<std::int16_t>(little_u16(bytes, offset));
}

std::int32_t little_i32(const std::span<const std::uint8_t> bytes, const std::size_t offset)
{
    return static_cast<std::int32_t>(little_u32(bytes, offset));
}

float little_f32(const std::span<const std::uint8_t> bytes, const std::size_t offset)
{
    float value = 0.0F;
    const std::uint32_t bits = little_u32(bytes, offset);
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

constexpr std::array<MavlinkFieldDefinition, 38> supported_fields{{
    {0U, "HEARTBEAT", "custom_mode", "Custom mode", "", true},
    {0U, "HEARTBEAT", "type", "Vehicle type", "", false},
    {0U, "HEARTBEAT", "autopilot", "Autopilot", "", false},
    {0U, "HEARTBEAT", "base_mode", "Base mode", "", true},
    {0U, "HEARTBEAT", "system_status", "System status", "", false},
    {0U, "HEARTBEAT", "mavlink_version", "MAVLink version", "", true},
    {24U, "GPS_RAW_INT", "time_usec", "Time", "us", true},
    {24U, "GPS_RAW_INT", "fix_type", "Fix type", "", false},
    {24U, "GPS_RAW_INT", "lat", "Latitude", "deg", true},
    {24U, "GPS_RAW_INT", "lon", "Longitude", "deg", true},
    {24U, "GPS_RAW_INT", "alt", "Altitude MSL", "m", true},
    {24U, "GPS_RAW_INT", "eph", "HDOP", "cm", true},
    {24U, "GPS_RAW_INT", "epv", "VDOP", "cm", true},
    {24U, "GPS_RAW_INT", "vel", "Ground speed", "m/s", true},
    {24U, "GPS_RAW_INT", "cog", "Course over ground", "deg", true},
    {24U, "GPS_RAW_INT", "satellites_visible", "Satellites", "", true},
    {30U, "ATTITUDE", "time_boot_ms", "Time boot", "ms", true},
    {30U, "ATTITUDE", "roll", "Roll", "rad", true},
    {30U, "ATTITUDE", "pitch", "Pitch", "rad", true},
    {30U, "ATTITUDE", "yaw", "Yaw", "rad", true},
    {30U, "ATTITUDE", "rollspeed", "Roll rate", "rad/s", true},
    {30U, "ATTITUDE", "pitchspeed", "Pitch rate", "rad/s", true},
    {30U, "ATTITUDE", "yawspeed", "Yaw rate", "rad/s", true},
    {33U, "GLOBAL_POSITION_INT", "time_boot_ms", "Time boot", "ms", true},
    {33U, "GLOBAL_POSITION_INT", "lat", "Latitude", "deg", true},
    {33U, "GLOBAL_POSITION_INT", "lon", "Longitude", "deg", true},
    {33U, "GLOBAL_POSITION_INT", "alt", "Altitude MSL", "m", true},
    {33U, "GLOBAL_POSITION_INT", "relative_alt", "Relative altitude", "m", true},
    {33U, "GLOBAL_POSITION_INT", "vx", "Velocity X", "m/s", true},
    {33U, "GLOBAL_POSITION_INT", "vy", "Velocity Y", "m/s", true},
    {33U, "GLOBAL_POSITION_INT", "vz", "Velocity Z", "m/s", true},
    {33U, "GLOBAL_POSITION_INT", "hdg", "Heading", "deg", true},
    {74U, "VFR_HUD", "airspeed", "Airspeed", "m/s", true},
    {74U, "VFR_HUD", "groundspeed", "Ground speed", "m/s", true},
    {74U, "VFR_HUD", "heading", "Heading", "deg", true},
    {74U, "VFR_HUD", "throttle", "Throttle", "%", true},
    {74U, "VFR_HUD", "alt", "Altitude", "m", true},
    {74U, "VFR_HUD", "climb", "Climb rate", "m/s", true},
}};

} // namespace

std::optional<std::uint8_t> mavlink_crc_extra(const std::uint32_t message_id)
{
    switch (message_id)
    {
    case 0:
        return 50;
    case 24:
        return 24;
    case 30:
        return 39;
    case 33:
        return 104;
    case 74:
        return 20;
    default:
        return std::nullopt;
    }
}

std::span<const MavlinkFieldDefinition> mavlink_supported_fields()
{
    return supported_fields;
}

const MavlinkFieldDefinition *mavlink_field_definition(const std::uint32_t message_id,
                                                       const std::string_view field_name)
{
    const auto found =
        std::find_if(supported_fields.begin(),
                     supported_fields.end(),
                     [message_id, field_name](const MavlinkFieldDefinition &field)
                     { return field.message_id == message_id && field.field_name == field_name; });
    return found == supported_fields.end() ? nullptr : &*found;
}

const MavlinkFieldDefinition *mavlink_field_definition(const std::string_view message_name,
                                                       const std::string_view field_name)
{
    const auto found = std::find_if(
        supported_fields.begin(),
        supported_fields.end(),
        [message_name, field_name](const MavlinkFieldDefinition &field)
        { return field.message_name == message_name && field.field_name == field_name; });
    return found == supported_fields.end() ? nullptr : &*found;
}

std::optional<double> mavlink_decode_numeric_field(const MavlinkMessage &message,
                                                   const std::string_view field_name)
{
    const MavlinkFieldDefinition *field = mavlink_field_definition(message.message_id, field_name);
    if (field == nullptr || !field->numeric)
    {
        return std::nullopt;
    }

    const std::span<const std::uint8_t> payload(message.payload);
    switch (message.message_id)
    {
    case 0U:
        if (payload.size() < 9U)
        {
            return std::nullopt;
        }
        if (field_name == "custom_mode")
        {
            return static_cast<double>(little_u32(payload, 0U));
        }
        if (field_name == "base_mode")
        {
            return static_cast<double>(payload[6U]);
        }
        if (field_name == "mavlink_version")
        {
            return static_cast<double>(payload[8U]);
        }
        break;
    case 24U:
        if (payload.size() < 30U)
        {
            return std::nullopt;
        }
        if (field_name == "time_usec")
        {
            return static_cast<double>(little_u64(payload, 0U));
        }
        if (field_name == "lat")
        {
            return static_cast<double>(little_i32(payload, 8U)) * 1.0e-7;
        }
        if (field_name == "lon")
        {
            return static_cast<double>(little_i32(payload, 12U)) * 1.0e-7;
        }
        if (field_name == "alt")
        {
            return static_cast<double>(little_i32(payload, 16U)) * 0.001;
        }
        if (field_name == "eph")
        {
            return static_cast<double>(little_u16(payload, 20U));
        }
        if (field_name == "epv")
        {
            return static_cast<double>(little_u16(payload, 22U));
        }
        if (field_name == "vel")
        {
            return static_cast<double>(little_u16(payload, 24U)) * 0.01;
        }
        if (field_name == "cog")
        {
            return static_cast<double>(little_u16(payload, 26U)) * 0.01;
        }
        if (field_name == "satellites_visible")
        {
            return static_cast<double>(payload[29U]);
        }
        break;
    case 30U:
        if (payload.size() < 28U)
        {
            return std::nullopt;
        }
        if (field_name == "time_boot_ms")
        {
            return static_cast<double>(little_u32(payload, 0U));
        }
        if (field_name == "roll")
        {
            return little_f32(payload, 4U);
        }
        if (field_name == "pitch")
        {
            return little_f32(payload, 8U);
        }
        if (field_name == "yaw")
        {
            return little_f32(payload, 12U);
        }
        if (field_name == "rollspeed")
        {
            return little_f32(payload, 16U);
        }
        if (field_name == "pitchspeed")
        {
            return little_f32(payload, 20U);
        }
        if (field_name == "yawspeed")
        {
            return little_f32(payload, 24U);
        }
        break;
    case 33U:
        if (payload.size() < 28U)
        {
            return std::nullopt;
        }
        if (field_name == "time_boot_ms")
        {
            return static_cast<double>(little_u32(payload, 0U));
        }
        if (field_name == "lat")
        {
            return static_cast<double>(little_i32(payload, 4U)) * 1.0e-7;
        }
        if (field_name == "lon")
        {
            return static_cast<double>(little_i32(payload, 8U)) * 1.0e-7;
        }
        if (field_name == "alt")
        {
            return static_cast<double>(little_i32(payload, 12U)) * 0.001;
        }
        if (field_name == "relative_alt")
        {
            return static_cast<double>(little_i32(payload, 16U)) * 0.001;
        }
        if (field_name == "vx")
        {
            return static_cast<double>(little_i16(payload, 20U)) * 0.01;
        }
        if (field_name == "vy")
        {
            return static_cast<double>(little_i16(payload, 22U)) * 0.01;
        }
        if (field_name == "vz")
        {
            return static_cast<double>(little_i16(payload, 24U)) * 0.01;
        }
        if (field_name == "hdg")
        {
            const std::uint16_t heading = little_u16(payload, 26U);
            if (heading == UINT16_MAX)
            {
                return std::nullopt;
            }
            return static_cast<double>(heading) * 0.01;
        }
        break;
    case 74U:
        if (payload.size() < 20U)
        {
            return std::nullopt;
        }
        if (field_name == "airspeed")
        {
            return little_f32(payload, 0U);
        }
        if (field_name == "groundspeed")
        {
            return little_f32(payload, 4U);
        }
        if (field_name == "heading")
        {
            return static_cast<double>(little_i16(payload, 8U));
        }
        if (field_name == "throttle")
        {
            return static_cast<double>(little_u16(payload, 10U));
        }
        if (field_name == "alt")
        {
            return little_f32(payload, 12U);
        }
        if (field_name == "climb")
        {
            return little_f32(payload, 16U);
        }
        break;
    default:
        break;
    }
    return std::nullopt;
}

MavlinkFieldObservationStatus mavlink_field_status(const MavlinkMessage &message,
                                                   const std::string_view field_name)
{
    const MavlinkFieldDefinition *field = mavlink_field_definition(message.message_id, field_name);
    if (field == nullptr)
    {
        return MavlinkFieldObservationStatus::Unsupported;
    }
    if (!field->numeric)
    {
        return MavlinkFieldObservationStatus::ObservedNonNumeric;
    }
    return mavlink_decode_numeric_field(message, field_name)
               ? MavlinkFieldObservationStatus::ObservedNumeric
               : MavlinkFieldObservationStatus::SupportedNotObserved;
}

std::uint16_t mavlink_crc_x25(const std::span<const std::uint8_t> bytes,
                              const std::uint8_t crc_extra)
{
    std::uint16_t crc = 0xFFFFU;
    for (const std::uint8_t byte : bytes)
    {
        crc_accumulate(byte, crc);
    }
    crc_accumulate(crc_extra, crc);
    return crc;
}

MavlinkParseResult parse_mavlink_stream(const std::span<const std::uint8_t> bytes)
{
    MavlinkParseResult result;
    std::size_t offset = 0U;
    while (offset < bytes.size())
    {
        const std::uint8_t magic = bytes[offset];
        if (magic != mavlink_v1_magic && magic != mavlink_v2_magic)
        {
            ++result.diagnostics.unsupported_versions;
            ++offset;
            continue;
        }

        const bool is_v2 = magic == mavlink_v2_magic;
        const std::size_t header_len = is_v2 ? 10U : 6U;
        if (bytes.size() - offset < header_len)
        {
            ++result.diagnostics.truncated_frames;
            break;
        }

        const std::size_t payload_len = bytes[offset + 1U];
        const std::size_t signature_len =
            is_v2 && ((bytes[offset + 2U] & mavlink_v2_signed_flag) != 0U) ? 13U : 0U;
        const std::size_t frame_len = header_len + payload_len + 2U + signature_len;
        if (bytes.size() - offset < frame_len)
        {
            ++result.diagnostics.truncated_frames;
            break;
        }
        if (is_v2 &&
            (bytes[offset + 2U] & static_cast<std::uint8_t>(~mavlink_v2_signed_flag)) != 0U)
        {
            ++result.diagnostics.malformed_frames;
            offset += frame_len;
            continue;
        }
        if (signature_len != 0U)
        {
            ++result.diagnostics.signed_v2_frames;
            offset += frame_len;
            continue;
        }

        const std::uint8_t sequence = bytes[offset + (is_v2 ? 4U : 2U)];
        const EntityId entity{bytes[offset + (is_v2 ? 5U : 3U)], bytes[offset + (is_v2 ? 6U : 4U)]};
        const std::uint32_t message_id =
            is_v2 ? (static_cast<std::uint32_t>(bytes[offset + 7U]) |
                     (static_cast<std::uint32_t>(bytes[offset + 8U]) << 8U) |
                     (static_cast<std::uint32_t>(bytes[offset + 9U]) << 16U))
                  : bytes[offset + 5U];
        const auto crc_extra = mavlink_crc_extra(message_id);
        if (!crc_extra)
        {
            ++result.diagnostics.unsupported_messages;
            MavlinkMessage message;
            message.version = is_v2 ? 2U : 1U;
            message.sequence = sequence;
            message.entity_id = entity;
            message.message_id = message_id;
            message.payload.assign(
                bytes.begin() + static_cast<std::ptrdiff_t>(offset + header_len),
                bytes.begin() + static_cast<std::ptrdiff_t>(offset + header_len + payload_len));
            result.messages.push_back(std::move(message));
            ++result.diagnostics.frames_decoded;
            offset += frame_len;
            continue;
        }

        const std::size_t crc_begin = offset + 1U;
        const std::size_t crc_size = header_len - 1U + payload_len;
        const std::uint16_t expected_crc = little_u16(bytes, offset + header_len + payload_len);
        const std::uint16_t actual_crc =
            mavlink_crc_x25(bytes.subspan(crc_begin, crc_size), *crc_extra);
        if (actual_crc != expected_crc)
        {
            ++result.diagnostics.crc_failures;
            ++offset;
            continue;
        }

        MavlinkMessage message;
        message.version = is_v2 ? 2U : 1U;
        message.sequence = sequence;
        message.entity_id = entity;
        message.message_id = message_id;
        message.payload.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset + header_len),
                               bytes.begin() +
                                   static_cast<std::ptrdiff_t>(offset + header_len + payload_len));
        result.messages.push_back(std::move(message));
        ++result.diagnostics.frames_decoded;
        offset += frame_len;
    }
    return result;
}

} // namespace animus::telemetry_core
