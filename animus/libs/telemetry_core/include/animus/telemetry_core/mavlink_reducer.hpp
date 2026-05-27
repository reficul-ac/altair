#pragma once

#include "animus/telemetry_core/mavlink.hpp"
#include "animus/telemetry_core/telemetry.hpp"

#include <optional>
#include <span>
#include <map>

namespace animus::telemetry_core
{

struct MavlinkTelemetryReducerConfig
{
    bool use_receive_time_for_untimed_messages = false;
    bool preserve_message_events = true;
    bool finalize_after_ingest = true;
};

class MavlinkTelemetryReducer
{
  public:
    struct PartialState
    {
        EntityId id;
        std::optional<double> lat_deg;
        std::optional<double> lon_deg;
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

    explicit MavlinkTelemetryReducer(MavlinkTelemetryReducerConfig config = {});

    [[nodiscard]] std::size_t ingest(std::span<const MavlinkMessage> messages,
                                     std::optional<double> receive_time_s = std::nullopt);
    [[nodiscard]] std::size_t ingest_parse_result(
        const MavlinkParseResult &parsed,
        std::optional<double> receive_time_s = std::nullopt);
    void finalize();

    [[nodiscard]] const Timeline &timeline() const;
    [[nodiscard]] Timeline release_timeline();
    [[nodiscard]] std::size_t prune(double history_seconds, std::size_t max_samples);

  private:
    MavlinkTelemetryReducerConfig config_;
    Timeline timeline_;
    std::map<EntityId, PartialState, bool (*)(EntityId, EntityId)> states_;
    double active_time_s_ = 0.0;
    bool have_active_time_ = false;
};

[[nodiscard]] Timeline reduce_mavlink_messages(std::span<const MavlinkMessage> messages,
                                               ParserDiagnostics diagnostics = {});

} // namespace animus::telemetry_core
