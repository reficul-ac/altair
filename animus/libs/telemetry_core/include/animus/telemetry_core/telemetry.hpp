#pragma once

#include <cstdint>
#include <compare>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace animus::telemetry_core
{

struct EntityId
{
    std::uint8_t system_id = 0;
    std::uint8_t component_id = 0;

    auto operator<=>(const EntityId &) const = default;
};

struct SourceFields
{
    bool position = false;
    bool altitude_msl = false;
    bool altitude_relative = false;
    bool attitude = false;
    bool velocity = false;
    bool heading = false;
};

struct TelemetrySample
{
    double time_s = 0.0;
    EntityId entity_id;
    double lat_deg = 0.0;
    double lon_deg = 0.0;
    std::optional<double> altitude_msl_m;
    std::optional<double> altitude_relative_m;
    std::optional<double> roll_rad;
    std::optional<double> pitch_rad;
    std::optional<double> yaw_rad;
    std::optional<double> ground_speed_mps;
    std::optional<double> climb_rate_mps;
    std::optional<double> heading_deg;
    SourceFields fields;
};

struct Entity
{
    EntityId id;
    std::optional<TelemetrySample> latest;
};

struct Track
{
    EntityId entity_id;
    std::vector<TelemetrySample> samples;
};

enum class EventSeverity
{
    Info,
    Warning,
    Error,
};

struct Event
{
    double time_s = 0.0;
    EntityId entity_id;
    std::uint32_t message_id = 0;
    EventSeverity severity = EventSeverity::Info;
    std::string message;
};

struct ParserDiagnostics
{
    std::uint64_t frames_decoded = 0;
    std::uint64_t unsupported_messages = 0;
    std::uint64_t crc_failures = 0;
    std::uint64_t truncated_frames = 0;
    std::uint64_t signed_v2_frames = 0;
    std::uint64_t unsupported_versions = 0;
    std::uint64_t malformed_frames = 0;
};

struct Timeline
{
    std::vector<TelemetrySample> samples;
    std::vector<Entity> entities;
    std::vector<Track> tracks;
    std::vector<Event> events;
    ParserDiagnostics diagnostics;
    double start_time_s = 0.0;
    double end_time_s = 0.0;

    [[nodiscard]] std::optional<TelemetrySample> sample_at(EntityId id, double time_s) const;
    [[nodiscard]] const Track *track_for(EntityId id) const;
};

class PlaybackClock
{
  public:
    void set_range(double start_time_s, double end_time_s);
    void set_rate(double rate);
    void set_paused(bool paused);
    void set_looping(bool looping);
    void seek(double time_s);
    void advance(double delta_wall_s);

    [[nodiscard]] double time_s() const;
    [[nodiscard]] double start_time_s() const;
    [[nodiscard]] double end_time_s() const;
    [[nodiscard]] double rate() const;
    [[nodiscard]] bool paused() const;
    [[nodiscard]] bool looping() const;

  private:
    double start_time_s_ = 0.0;
    double end_time_s_ = 0.0;
    double time_s_ = 0.0;
    double rate_ = 1.0;
    bool paused_ = true;
    bool looping_ = false;
};

[[nodiscard]] Timeline load_tlog(const std::filesystem::path &path);
[[nodiscard]] Timeline load_tlog_bytes(const std::vector<std::uint8_t> &bytes);

} // namespace animus::telemetry_core
