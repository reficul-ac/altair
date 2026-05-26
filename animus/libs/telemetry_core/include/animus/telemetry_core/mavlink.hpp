#pragma once

#include "animus/telemetry_core/telemetry.hpp"

#include <cstdint>
#include <optional>
#include <span>
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

[[nodiscard]] MavlinkParseResult parse_mavlink_stream(std::span<const std::uint8_t> bytes);

[[nodiscard]] std::uint16_t mavlink_crc_x25(std::span<const std::uint8_t> bytes,
                                            std::uint8_t crc_extra);
[[nodiscard]] std::optional<std::uint8_t> mavlink_crc_extra(std::uint32_t message_id);

} // namespace animus::telemetry_core
