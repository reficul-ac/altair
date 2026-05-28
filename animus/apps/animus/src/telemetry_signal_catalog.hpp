#pragma once

#include "animus/telemetry_core/mavlink.hpp"
#include "animus/telemetry_core/telemetry.hpp"
#include "animus/telemetry_live/live_telemetry_buffer.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <functional>
#include <vector>

namespace animus::app
{

enum class SignalSource
{
    Sample,
    Derived,
    Runtime,
    Mavlink,
};

enum class SignalTransform
{
    None,
    RadToDeg,
    DegToRad,
    MetersToFeet,
    MpsToKts,
    MpsToMph,
    Abs,
    Negate,
};

enum class SignalSampleStatus
{
    Valid,
    Unavailable,
    Unsupported,
    NonNumeric,
};

struct SignalRef
{
    SignalSource source = SignalSource::Sample;
    std::string field_path;
    std::string mavlink_message;
    std::string mavlink_field;
    std::optional<animus::telemetry_core::EntityId> entity_id;

    bool operator==(const SignalRef &) const = default;
};

struct SignalInfo
{
    SignalRef ref;
    std::string display_name;
    std::string unit;
    bool numeric = true;
    SignalTransform default_transform = SignalTransform::None;
    bool live_available = true;
    bool offline_available = true;
};

struct SignalSample
{
    double time_s = 0.0;
    double value = 0.0;
    SignalSampleStatus status = SignalSampleStatus::Unavailable;
};

struct RuntimeSignalInputs
{
    std::optional<double> terrain_elevation_m;
    std::optional<double> terrain_clearance_m;
    std::optional<double> link_hz;
    std::optional<double> telemetry_age_s;
    std::optional<double> telemetry_gap_s;
    std::uint64_t packet_count = 0;
    std::uint64_t drop_count = 0;
    double frame_time_ms = 0.0;
    std::size_t resident_tile_count = 0U;
    std::size_t upload_bytes_this_frame = 0U;
};

struct MavlinkValueStoreConfig
{
    std::size_t max_samples_per_field = 512U;
    double history_seconds = 120.0;
};

struct MavlinkFieldKey
{
    animus::telemetry_core::EntityId entity_id;
    std::uint32_t message_id = 0;
    std::string field_name;

    auto operator<=>(const MavlinkFieldKey &) const = default;
};

struct MavlinkStoredSample
{
    double time_s = 0.0;
    double value = 0.0;
};

struct MavlinkValueStats
{
    animus::telemetry_core::MavlinkFieldObservationStatus status =
        animus::telemetry_core::MavlinkFieldObservationStatus::SupportedNotObserved;
    std::uint64_t count = 0;
    std::optional<double> latest_value;
    double latest_time_s = 0.0;
    double approximate_hz = 0.0;
    double last_age_s = 0.0;
    std::optional<double> min_value;
    std::optional<double> max_value;
    std::size_t retained_samples = 0U;
};

class MavlinkValueStore
{
  public:
    explicit MavlinkValueStore(MavlinkValueStoreConfig config = {});

    void ingest(const animus::telemetry_live::ParsedUdpMavlinkDatagram &datagram);
    void ingest(std::span<const animus::telemetry_live::ParsedUdpMavlinkDatagram> datagrams);
    void ingest_messages(std::span<const animus::telemetry_core::MavlinkMessage> messages,
                         std::optional<double> receive_time_s = std::nullopt);

    [[nodiscard]] MavlinkValueStats stats(const animus::telemetry_core::EntityId &entity_id,
                                          std::uint32_t message_id,
                                          const std::string &field_name,
                                          double now_s) const;
    [[nodiscard]] std::optional<MavlinkStoredSample>
    latest(const animus::telemetry_core::EntityId &entity_id,
           std::uint32_t message_id,
           const std::string &field_name) const;
    void for_each_sample(const animus::telemetry_core::EntityId &entity_id,
                         std::uint32_t message_id,
                         const std::string &field_name,
                         const std::function<void(const MavlinkStoredSample &)> &callback) const;

  private:
    struct FieldState
    {
        animus::telemetry_core::MavlinkFieldObservationStatus status =
            animus::telemetry_core::MavlinkFieldObservationStatus::SupportedNotObserved;
        std::uint64_t count = 0;
        std::deque<MavlinkStoredSample> history;
        std::optional<double> latest_value;
        double latest_time_s = 0.0;
        std::optional<double> min_value;
        std::optional<double> max_value;
    };

    void record_numeric(const MavlinkFieldKey &key, double time_s, double value);
    void record_nonnumeric(const MavlinkFieldKey &key, double time_s);
    void prune(FieldState &state, double newest_time_s) const;

    MavlinkValueStoreConfig config_;
    std::map<MavlinkFieldKey, FieldState> fields_;
};

class SignalCatalog
{
  public:
    SignalCatalog();

    [[nodiscard]] const std::vector<SignalInfo> &signals() const;
    [[nodiscard]] const SignalInfo *lookup(const SignalRef &ref) const;
    [[nodiscard]] SignalSample extract_sample(const SignalRef &ref,
                                              const animus::telemetry_core::TelemetrySample &sample,
                                              SignalTransform transform) const;
    [[nodiscard]] SignalSample extract_runtime(const SignalRef &ref,
                                               const RuntimeSignalInputs &runtime,
                                               double time_s,
                                               SignalTransform transform) const;
    [[nodiscard]] SignalSample extract_mavlink(const SignalRef &ref,
                                               const MavlinkValueStore &store,
                                               double now_s,
                                               SignalTransform transform) const;

    [[nodiscard]] static double apply_transform(double value, SignalTransform transform);
    [[nodiscard]] static const char *source_name(SignalSource source);
    [[nodiscard]] static const char *transform_name(SignalTransform transform);
    [[nodiscard]] static const char *status_name(SignalSampleStatus status);

  private:
    std::vector<SignalInfo> signals_;
};

[[nodiscard]] std::uint32_t mavlink_message_id(std::string_view message_name);

} // namespace animus::app
