#include "telemetry_signal_catalog.hpp"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace animus::app
{
namespace
{

using animus::telemetry_core::MavlinkFieldObservationStatus;
using animus::telemetry_core::TelemetrySample;

SignalInfo make_sample_signal(const char *path,
                              const char *display,
                              const char *unit,
                              const SignalTransform transform = SignalTransform::None)
{
    SignalRef ref;
    ref.source = SignalSource::Sample;
    ref.field_path = path;
    return SignalInfo{
        .ref = ref,
        .display_name = display,
        .unit = unit,
        .numeric = true,
        .default_transform = transform,
        .live_available = true,
        .offline_available = true,
    };
}

SignalInfo make_runtime_signal(
    const char *path, const char *display, const char *unit, const bool live, const bool offline)
{
    SignalRef ref;
    ref.source = SignalSource::Runtime;
    ref.field_path = path;
    return SignalInfo{
        .ref = ref,
        .display_name = display,
        .unit = unit,
        .numeric = true,
        .default_transform = SignalTransform::None,
        .live_available = live,
        .offline_available = offline,
    };
}

SignalInfo make_mavlink_signal(const animus::telemetry_core::MavlinkFieldDefinition &field)
{
    SignalTransform transform = SignalTransform::None;
    if (field.unit == "rad")
    {
        transform = SignalTransform::RadToDeg;
    }
    SignalRef ref;
    ref.source = SignalSource::Mavlink;
    ref.field_path =
        "mavlink." + std::string(field.message_name) + "." + std::string(field.field_name);
    ref.mavlink_message = std::string(field.message_name);
    ref.mavlink_field = std::string(field.field_name);
    return SignalInfo{
        .ref = ref,
        .display_name = std::string(field.message_name) + " " + std::string(field.display_name),
        .unit = std::string(field.unit),
        .numeric = field.numeric,
        .default_transform = transform,
        .live_available = true,
        .offline_available = true,
    };
}

SignalSample valid_sample(const double time_s,
                          const std::optional<double> value,
                          const SignalTransform transform)
{
    if (!value)
    {
        return SignalSample{.time_s = time_s, .status = SignalSampleStatus::Unavailable};
    }
    return SignalSample{
        .time_s = time_s,
        .value = SignalCatalog::apply_transform(*value, transform),
        .status = SignalSampleStatus::Valid,
    };
}

std::optional<double> sample_field_value(const TelemetrySample &sample, const std::string &path)
{
    if (path == "time_s")
    {
        return sample.time_s;
    }
    if (path == "lat_deg")
    {
        return sample.fields.position ? std::optional<double>(sample.lat_deg) : std::nullopt;
    }
    if (path == "lon_deg")
    {
        return sample.fields.position ? std::optional<double>(sample.lon_deg) : std::nullopt;
    }
    if (path == "altitude_msl_m")
    {
        return sample.altitude_msl_m;
    }
    if (path == "altitude_relative_m")
    {
        return sample.altitude_relative_m;
    }
    if (path == "roll_rad")
    {
        return sample.roll_rad;
    }
    if (path == "pitch_rad")
    {
        return sample.pitch_rad;
    }
    if (path == "yaw_rad")
    {
        return sample.yaw_rad;
    }
    if (path == "ground_speed_mps")
    {
        return sample.ground_speed_mps;
    }
    if (path == "climb_rate_mps")
    {
        return sample.climb_rate_mps;
    }
    if (path == "heading_deg")
    {
        return sample.heading_deg;
    }
    return std::nullopt;
}

std::optional<double> runtime_field_value(const RuntimeSignalInputs &runtime,
                                          const std::string &path)
{
    if (path == "terrain_elevation_m")
    {
        return runtime.terrain_elevation_m;
    }
    if (path == "terrain_clearance_m")
    {
        return runtime.terrain_clearance_m;
    }
    if (path == "link_hz")
    {
        return runtime.link_hz;
    }
    if (path == "telemetry_age_s")
    {
        return runtime.telemetry_age_s;
    }
    if (path == "telemetry_gap_s")
    {
        return runtime.telemetry_gap_s;
    }
    if (path == "packet_count")
    {
        return static_cast<double>(runtime.packet_count);
    }
    if (path == "drop_count")
    {
        return static_cast<double>(runtime.drop_count);
    }
    if (path == "frame_time_ms")
    {
        return runtime.frame_time_ms;
    }
    if (path == "resident_tile_count")
    {
        return static_cast<double>(runtime.resident_tile_count);
    }
    if (path == "upload_bytes_this_frame")
    {
        return static_cast<double>(runtime.upload_bytes_this_frame);
    }
    return std::nullopt;
}

double message_time_s(const animus::telemetry_core::MavlinkMessage &message,
                      const std::optional<double> receive_time_s)
{
    const std::optional<double> decoded =
        animus::telemetry_core::mavlink_decode_numeric_field(message, "time_boot_ms");
    if (decoded)
    {
        return *decoded * 0.001;
    }
    const std::optional<double> gps_time =
        animus::telemetry_core::mavlink_decode_numeric_field(message, "time_usec");
    if (gps_time)
    {
        return *gps_time * 0.000001;
    }
    return receive_time_s.value_or(0.0);
}

std::string mavlink_message_name(const std::uint32_t message_id)
{
    for (const animus::telemetry_core::MavlinkFieldDefinition &field :
         animus::telemetry_core::mavlink_supported_fields())
    {
        if (field.message_id == message_id)
        {
            return std::string(field.message_name);
        }
    }
    return {};
}

double approximate_hz(const std::deque<double> &times)
{
    if (times.size() < 2U)
    {
        return 0.0;
    }
    const double span_s = times.back() - times.front();
    return span_s > 0.0 ? static_cast<double>(times.size() - 1U) / span_s : 0.0;
}

} // namespace

std::uint32_t mavlink_message_id(const std::string_view message_name)
{
    for (const animus::telemetry_core::MavlinkFieldDefinition &field :
         animus::telemetry_core::mavlink_supported_fields())
    {
        if (field.message_name == message_name)
        {
            return field.message_id;
        }
    }
    return UINT32_MAX;
}

MavlinkValueStore::MavlinkValueStore(MavlinkValueStoreConfig config) : config_(config)
{
}

void MavlinkValueStore::ingest(const animus::telemetry_live::ParsedUdpMavlinkDatagram &datagram)
{
    ingest_messages(datagram.parsed.messages, datagram.receive_time_s);
}

void MavlinkValueStore::ingest(
    const std::span<const animus::telemetry_live::ParsedUdpMavlinkDatagram> datagrams)
{
    for (const animus::telemetry_live::ParsedUdpMavlinkDatagram &datagram : datagrams)
    {
        ingest(datagram);
    }
}

void MavlinkValueStore::ingest_messages(
    const std::span<const animus::telemetry_core::MavlinkMessage> messages,
    const std::optional<double> receive_time_s)
{
    for (const animus::telemetry_core::MavlinkMessage &message : messages)
    {
        const double time_s = message_time_s(message, receive_time_s);
        const std::string message_name = mavlink_message_name(message.message_id);
        if (!message_name.empty())
        {
            record_message(
                MessageKey{.entity_id = message.entity_id, .message_id = message.message_id},
                message_name,
                time_s);
        }
        for (const animus::telemetry_core::MavlinkFieldDefinition &field :
             animus::telemetry_core::mavlink_supported_fields())
        {
            if (field.message_id != message.message_id)
            {
                continue;
            }
            const MavlinkFieldKey key{
                .entity_id = message.entity_id,
                .message_id = message.message_id,
                .field_name = std::string(field.field_name),
            };
            if (!field.numeric)
            {
                record_nonnumeric(key, time_s);
                continue;
            }
            const std::optional<double> value =
                animus::telemetry_core::mavlink_decode_numeric_field(message, field.field_name);
            if (value)
            {
                record_numeric(key, time_s, *value);
            }
            else
            {
                fields_[key].status = MavlinkFieldObservationStatus::SupportedNotObserved;
            }
        }
    }
}

MavlinkValueStats MavlinkValueStore::stats(const animus::telemetry_core::EntityId &entity_id,
                                           const std::uint32_t message_id,
                                           const std::string &field_name,
                                           const double now_s) const
{
    const MavlinkFieldKey key{
        .entity_id = entity_id,
        .message_id = message_id,
        .field_name = field_name,
    };
    const auto found = fields_.find(key);
    if (found == fields_.end())
    {
        return {};
    }
    const FieldState &state = found->second;
    MavlinkValueStats result;
    result.status = state.status;
    result.count = state.count;
    result.latest_value = state.latest_value;
    result.latest_time_s = state.latest_time_s;
    result.last_changed_time_s = state.last_changed_time_s;
    result.last_age_s = std::max(0.0, now_s - state.latest_time_s);
    result.last_changed_age_s = std::max(0.0, now_s - state.last_changed_time_s);
    result.min_value = state.min_value;
    result.max_value = state.max_value;
    result.retained_samples = state.history.size();
    if (state.history.size() >= 2U)
    {
        const double span_s = state.history.back().time_s - state.history.front().time_s;
        if (span_s > 0.0)
        {
            result.approximate_hz = static_cast<double>(state.history.size() - 1U) / span_s;
        }
    }
    return result;
}

std::vector<MavlinkMessageStats>
MavlinkValueStore::observed_messages(const animus::telemetry_core::EntityId &entity_id,
                                     const double now_s) const
{
    std::vector<MavlinkMessageStats> result;
    for (const auto &[key, state] : messages_)
    {
        if (key.entity_id != entity_id)
        {
            continue;
        }
        MavlinkMessageStats stats;
        stats.entity_id = key.entity_id;
        stats.message_id = key.message_id;
        stats.message_name = state.message_name;
        stats.approximate_hz = approximate_hz(state.history_times_s);
        stats.last_age_s = std::max(0.0, now_s - state.latest_time_s);
        stats.count = state.count;
        bool observed_nonnumeric = false;
        bool supported_not_observed = false;
        for (const auto &[field_key, field_state] : fields_)
        {
            if (field_key.entity_id != key.entity_id || field_key.message_id != key.message_id)
            {
                continue;
            }
            if (field_state.status == MavlinkFieldObservationStatus::ObservedNumeric)
            {
                ++stats.observed_numeric_field_count;
            }
            else if (field_state.status == MavlinkFieldObservationStatus::ObservedNonNumeric)
            {
                observed_nonnumeric = true;
            }
            else if (field_state.status == MavlinkFieldObservationStatus::SupportedNotObserved)
            {
                supported_not_observed = true;
            }
        }
        if (stats.observed_numeric_field_count > 0U)
        {
            stats.status = MavlinkFieldObservationStatus::ObservedNumeric;
        }
        else if (observed_nonnumeric)
        {
            stats.status = MavlinkFieldObservationStatus::ObservedNonNumeric;
        }
        else if (supported_not_observed)
        {
            stats.status = MavlinkFieldObservationStatus::SupportedNotObserved;
        }
        result.push_back(std::move(stats));
    }
    std::sort(result.begin(),
              result.end(),
              [](const MavlinkMessageStats &lhs, const MavlinkMessageStats &rhs)
              {
                  if (lhs.message_name != rhs.message_name)
                  {
                      return lhs.message_name < rhs.message_name;
                  }
                  return lhs.message_id < rhs.message_id;
              });
    return result;
}

std::vector<MavlinkInspectorFieldStats>
MavlinkValueStore::observed_fields(const animus::telemetry_core::EntityId &entity_id,
                                   const std::uint32_t message_id,
                                   const double now_s) const
{
    std::vector<MavlinkInspectorFieldStats> result;
    for (const animus::telemetry_core::MavlinkFieldDefinition &definition :
         animus::telemetry_core::mavlink_supported_fields())
    {
        if (definition.message_id != message_id)
        {
            continue;
        }
        const MavlinkFieldKey key{
            .entity_id = entity_id,
            .message_id = message_id,
            .field_name = std::string(definition.field_name),
        };
        const auto found = fields_.find(key);
        if (found == fields_.end())
        {
            continue;
        }
        const FieldState &state = found->second;
        result.push_back(MavlinkInspectorFieldStats{
            .entity_id = entity_id,
            .message_id = message_id,
            .message_name = std::string(definition.message_name),
            .field_name = std::string(definition.field_name),
            .display_name = std::string(definition.display_name),
            .unit = std::string(definition.unit),
            .numeric = definition.numeric,
            .status = state.status,
            .count = state.count,
            .latest_value = state.latest_value,
            .latest_time_s = state.latest_time_s,
            .last_age_s = std::max(0.0, now_s - state.latest_time_s),
            .last_changed_time_s = state.last_changed_time_s,
            .last_changed_age_s = std::max(0.0, now_s - state.last_changed_time_s),
            .min_value = state.min_value,
            .max_value = state.max_value,
        });
    }
    return result;
}

std::optional<MavlinkStoredSample>
MavlinkValueStore::latest(const animus::telemetry_core::EntityId &entity_id,
                          const std::uint32_t message_id,
                          const std::string &field_name) const
{
    const MavlinkFieldKey key{
        .entity_id = entity_id,
        .message_id = message_id,
        .field_name = field_name,
    };
    const auto found = fields_.find(key);
    if (found == fields_.end() || !found->second.latest_value)
    {
        return std::nullopt;
    }
    return MavlinkStoredSample{.time_s = found->second.latest_time_s,
                               .value = *found->second.latest_value};
}

void MavlinkValueStore::for_each_sample(
    const animus::telemetry_core::EntityId &entity_id,
    const std::uint32_t message_id,
    const std::string &field_name,
    const std::function<void(const MavlinkStoredSample &)> &callback) const
{
    const MavlinkFieldKey key{
        .entity_id = entity_id,
        .message_id = message_id,
        .field_name = field_name,
    };
    const auto found = fields_.find(key);
    if (found == fields_.end())
    {
        return;
    }
    for (const MavlinkStoredSample &sample : found->second.history)
    {
        callback(sample);
    }
}

void MavlinkValueStore::record_numeric(const MavlinkFieldKey &key,
                                       const double time_s,
                                       const double value)
{
    FieldState &state = fields_[key];
    state.status = MavlinkFieldObservationStatus::ObservedNumeric;
    const bool changed = !state.latest_value || *state.latest_value != value;
    ++state.count;
    state.latest_value = value;
    state.latest_time_s = time_s;
    if (changed)
    {
        state.last_changed_time_s = time_s;
    }
    state.min_value = state.min_value ? std::min(*state.min_value, value) : value;
    state.max_value = state.max_value ? std::max(*state.max_value, value) : value;
    state.history.push_back(MavlinkStoredSample{.time_s = time_s, .value = value});
    prune(state, time_s);
}

void MavlinkValueStore::record_nonnumeric(const MavlinkFieldKey &key, const double time_s)
{
    FieldState &state = fields_[key];
    state.status = MavlinkFieldObservationStatus::ObservedNonNumeric;
    ++state.count;
    state.latest_time_s = time_s;
    state.last_changed_time_s = time_s;
}

void MavlinkValueStore::record_message(const MessageKey &key,
                                       std::string message_name,
                                       const double time_s)
{
    MessageState &state = messages_[key];
    state.message_name = std::move(message_name);
    ++state.count;
    state.latest_time_s = time_s;
    state.history_times_s.push_back(time_s);
    prune(state, time_s);
}

void MavlinkValueStore::prune(FieldState &state, const double newest_time_s) const
{
    if (config_.history_seconds > 0.0)
    {
        const double oldest = newest_time_s - config_.history_seconds;
        while (!state.history.empty() && state.history.front().time_s < oldest)
        {
            state.history.pop_front();
        }
    }
    while (config_.max_samples_per_field > 0U &&
           state.history.size() > config_.max_samples_per_field)
    {
        state.history.pop_front();
    }
}

void MavlinkValueStore::prune(MessageState &state, const double newest_time_s) const
{
    if (config_.history_seconds > 0.0)
    {
        const double oldest = newest_time_s - config_.history_seconds;
        while (!state.history_times_s.empty() && state.history_times_s.front() < oldest)
        {
            state.history_times_s.pop_front();
        }
    }
    while (config_.max_samples_per_field > 0U &&
           state.history_times_s.size() > config_.max_samples_per_field)
    {
        state.history_times_s.pop_front();
    }
}

SignalCatalog::SignalCatalog()
{
    signals_.push_back(make_sample_signal("time_s", "Time", "s"));
    signals_.push_back(make_sample_signal("lat_deg", "Latitude", "deg"));
    signals_.push_back(make_sample_signal("lon_deg", "Longitude", "deg"));
    signals_.push_back(make_sample_signal("altitude_msl_m", "Altitude MSL", "m"));
    signals_.push_back(make_sample_signal("altitude_relative_m", "Relative altitude", "m"));
    signals_.push_back(make_sample_signal("roll_rad", "Roll", "rad", SignalTransform::RadToDeg));
    signals_.push_back(make_sample_signal("pitch_rad", "Pitch", "rad", SignalTransform::RadToDeg));
    signals_.push_back(make_sample_signal("yaw_rad", "Yaw", "rad", SignalTransform::RadToDeg));
    signals_.push_back(make_sample_signal("ground_speed_mps", "Ground speed", "m/s"));
    signals_.push_back(make_sample_signal("climb_rate_mps", "Climb rate", "m/s"));
    signals_.push_back(make_sample_signal("heading_deg", "Heading", "deg"));

    SignalRef terrain_elevation;
    terrain_elevation.source = SignalSource::Derived;
    terrain_elevation.field_path = "terrain_elevation_m";
    signals_.push_back(SignalInfo{.ref = terrain_elevation,
                                  .display_name = "Terrain elevation",
                                  .unit = "m",
                                  .numeric = true,
                                  .default_transform = SignalTransform::None,
                                  .live_available = true,
                                  .offline_available = true});
    SignalRef terrain_clearance;
    terrain_clearance.source = SignalSource::Derived;
    terrain_clearance.field_path = "terrain_clearance_m";
    signals_.push_back(SignalInfo{.ref = terrain_clearance,
                                  .display_name = "Terrain clearance",
                                  .unit = "m",
                                  .numeric = true,
                                  .default_transform = SignalTransform::None,
                                  .live_available = true,
                                  .offline_available = true});

    signals_.push_back(make_runtime_signal("link_hz", "Link rate", "Hz", true, false));
    signals_.push_back(make_runtime_signal("telemetry_age_s", "Telemetry age", "s", true, true));
    signals_.push_back(make_runtime_signal("telemetry_gap_s", "Telemetry gap", "s", true, true));
    signals_.push_back(make_runtime_signal("packet_count", "Packet count", "", true, false));
    signals_.push_back(make_runtime_signal("drop_count", "Drop count", "", true, false));
    signals_.push_back(make_runtime_signal("frame_time_ms", "Frame time", "ms", true, true));
    signals_.push_back(
        make_runtime_signal("resident_tile_count", "Resident tiles", "", true, true));
    signals_.push_back(
        make_runtime_signal("upload_bytes_this_frame", "Upload bytes this frame", "B", true, true));

    for (const animus::telemetry_core::MavlinkFieldDefinition &field :
         animus::telemetry_core::mavlink_supported_fields())
    {
        signals_.push_back(make_mavlink_signal(field));
    }
}

const std::vector<SignalInfo> &SignalCatalog::signals() const
{
    return signals_;
}

const SignalInfo *SignalCatalog::lookup(const SignalRef &ref) const
{
    const auto found =
        std::find_if(signals_.begin(),
                     signals_.end(),
                     [&ref](const SignalInfo &info)
                     {
                         if (info.ref.source != ref.source)
                         {
                             return false;
                         }
                         if (ref.source == SignalSource::Mavlink)
                         {
                             return info.ref.mavlink_message == ref.mavlink_message &&
                                    info.ref.mavlink_field == ref.mavlink_field;
                         }
                         return info.ref.field_path == ref.field_path;
                     });
    return found == signals_.end() ? nullptr : &*found;
}

SignalSample SignalCatalog::extract_sample(const SignalRef &ref,
                                           const TelemetrySample &sample,
                                           const SignalTransform transform) const
{
    if (ref.source != SignalSource::Sample)
    {
        return SignalSample{.time_s = sample.time_s, .status = SignalSampleStatus::Unsupported};
    }
    if (lookup(ref) == nullptr)
    {
        return SignalSample{.time_s = sample.time_s, .status = SignalSampleStatus::Unsupported};
    }
    return valid_sample(sample.time_s, sample_field_value(sample, ref.field_path), transform);
}

SignalSample SignalCatalog::extract_runtime(const SignalRef &ref,
                                            const RuntimeSignalInputs &runtime,
                                            const double time_s,
                                            const SignalTransform transform) const
{
    if (ref.source != SignalSource::Runtime && ref.source != SignalSource::Derived)
    {
        return SignalSample{.time_s = time_s, .status = SignalSampleStatus::Unsupported};
    }
    if (lookup(ref) == nullptr)
    {
        return SignalSample{.time_s = time_s, .status = SignalSampleStatus::Unsupported};
    }
    return valid_sample(time_s, runtime_field_value(runtime, ref.field_path), transform);
}

SignalSample SignalCatalog::extract_mavlink(const SignalRef &ref,
                                            const MavlinkValueStore &store,
                                            const double now_s,
                                            const SignalTransform transform) const
{
    const SignalInfo *info = lookup(ref);
    if (ref.source != SignalSource::Mavlink || info == nullptr)
    {
        return SignalSample{.time_s = now_s, .status = SignalSampleStatus::Unsupported};
    }
    if (!info->numeric)
    {
        return SignalSample{.time_s = now_s, .status = SignalSampleStatus::NonNumeric};
    }
    if (!ref.entity_id)
    {
        return SignalSample{.time_s = now_s, .status = SignalSampleStatus::Unavailable};
    }
    const std::uint32_t message_id = mavlink_message_id(ref.mavlink_message);
    const std::optional<MavlinkStoredSample> latest =
        store.latest(*ref.entity_id, message_id, ref.mavlink_field);
    if (!latest)
    {
        return SignalSample{.time_s = now_s, .status = SignalSampleStatus::Unavailable};
    }
    return SignalSample{.time_s = latest->time_s,
                        .value = apply_transform(latest->value, transform),
                        .status = SignalSampleStatus::Valid};
}

double SignalCatalog::apply_transform(const double value, const SignalTransform transform)
{
    switch (transform)
    {
    case SignalTransform::None:
        return value;
    case SignalTransform::RadToDeg:
        return value * 180.0 / 3.14159265358979323846;
    case SignalTransform::DegToRad:
        return value * 3.14159265358979323846 / 180.0;
    case SignalTransform::MetersToFeet:
        return value * 3.280839895;
    case SignalTransform::MpsToKts:
        return value * 1.943844492;
    case SignalTransform::MpsToMph:
        return value * 2.236936292;
    case SignalTransform::Abs:
        return std::abs(value);
    case SignalTransform::Negate:
        return -value;
    }
    return value;
}

const char *SignalCatalog::source_name(const SignalSource source)
{
    switch (source)
    {
    case SignalSource::Sample:
        return "sample";
    case SignalSource::Derived:
        return "derived";
    case SignalSource::Runtime:
        return "runtime";
    case SignalSource::Mavlink:
        return "mavlink";
    }
    return "sample";
}

const char *SignalCatalog::transform_name(const SignalTransform transform)
{
    switch (transform)
    {
    case SignalTransform::None:
        return "none";
    case SignalTransform::RadToDeg:
        return "rad_to_deg";
    case SignalTransform::DegToRad:
        return "deg_to_rad";
    case SignalTransform::MetersToFeet:
        return "meters_to_feet";
    case SignalTransform::MpsToKts:
        return "mps_to_kts";
    case SignalTransform::MpsToMph:
        return "mps_to_mph";
    case SignalTransform::Abs:
        return "abs";
    case SignalTransform::Negate:
        return "negate";
    }
    return "none";
}

const char *SignalCatalog::status_name(const SignalSampleStatus status)
{
    switch (status)
    {
    case SignalSampleStatus::Valid:
        return "valid";
    case SignalSampleStatus::Unavailable:
        return "unavailable";
    case SignalSampleStatus::Unsupported:
        return "unsupported";
    case SignalSampleStatus::NonNumeric:
        return "non-numeric";
    }
    return "unavailable";
}

} // namespace animus::app
