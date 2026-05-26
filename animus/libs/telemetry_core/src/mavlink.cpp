#include "animus/telemetry_core/mavlink.hpp"

#include <array>
#include <cstddef>

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
        if (is_v2 && (bytes[offset + 2U] & static_cast<std::uint8_t>(~mavlink_v2_signed_flag)) !=
                         0U)
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
        const EntityId entity{bytes[offset + (is_v2 ? 5U : 3U)],
                              bytes[offset + (is_v2 ? 6U : 4U)]};
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
            message.payload.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset + header_len),
                                   bytes.begin() + static_cast<std::ptrdiff_t>(
                                                     offset + header_len + payload_len));
            result.messages.push_back(std::move(message));
            ++result.diagnostics.frames_decoded;
            offset += frame_len;
            continue;
        }

        const std::size_t crc_begin = offset + 1U;
        const std::size_t crc_size = header_len - 1U + payload_len;
        const std::uint16_t expected_crc =
            little_u16(bytes, offset + header_len + payload_len);
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
                               bytes.begin() + static_cast<std::ptrdiff_t>(
                                                 offset + header_len + payload_len));
        result.messages.push_back(std::move(message));
        ++result.diagnostics.frames_decoded;
        offset += frame_len;
    }
    return result;
}

} // namespace animus::telemetry_core
