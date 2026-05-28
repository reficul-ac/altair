#pragma once

#include "animus/telemetry_core/telemetry.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace animus::telemetry_core
{

struct MavlinkMessage
{
    std::uint8_t version = 0;
    std::uint8_t sequence = 0;
    EntityId entity_id;
    std::uint32_t message_id = 0;
    std::vector<std::uint8_t> payload;
};

struct MavlinkParseResult
{
    std::vector<MavlinkMessage> messages;
    ParserDiagnostics diagnostics;
};

enum class MavlinkFieldObservationStatus
{
    Unsupported,
    SupportedNotObserved,
    ObservedNumeric,
    ObservedNonNumeric,
};

struct MavlinkFieldDefinition
{
    std::uint32_t message_id = 0;
    std::string_view message_name;
    std::string_view field_name;
    std::string_view display_name;
    std::string_view unit;
    bool numeric = false;
};

[[nodiscard]] MavlinkParseResult parse_mavlink_stream(std::span<const std::uint8_t> bytes);

[[nodiscard]] std::uint16_t mavlink_crc_x25(std::span<const std::uint8_t> bytes,
                                            std::uint8_t crc_extra);
[[nodiscard]] std::optional<std::uint8_t> mavlink_crc_extra(std::uint32_t message_id);
[[nodiscard]] std::span<const MavlinkFieldDefinition> mavlink_supported_fields();
[[nodiscard]] const MavlinkFieldDefinition *mavlink_field_definition(std::uint32_t message_id,
                                                                     std::string_view field_name);
[[nodiscard]] const MavlinkFieldDefinition *mavlink_field_definition(std::string_view message_name,
                                                                     std::string_view field_name);
[[nodiscard]] std::optional<double> mavlink_decode_numeric_field(const MavlinkMessage &message,
                                                                 std::string_view field_name);
[[nodiscard]] MavlinkFieldObservationStatus mavlink_field_status(const MavlinkMessage &message,
                                                                 std::string_view field_name);

} // namespace animus::telemetry_core
