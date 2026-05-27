#include "animus/telemetry_core/telemetry.hpp"

#include "canonical_proto.hpp"
#include "timeline_builder.hpp"

#define MCAP_IMPLEMENTATION
#include <mcap/reader.hpp>
#include <mcap/writer.hpp>

#include <cstddef>
#include <span>
#include <stdexcept>
#include <string>

namespace animus::telemetry_core
{
namespace
{

bool is_supported_channel(const mcap::Channel &channel, const mcap::Schema *schema)
{
    return schema != nullptr && schema->encoding == "protobuf" &&
           schema->name == canonical_protobuf_schema_name() &&
           channel.messageEncoding == "protobuf";
}

} // namespace

Timeline load_mcap_protobuf(const std::filesystem::path &path)
{
    Timeline timeline;
    timeline.source_format = TelemetryImportFormat::McapProtobuf;

    mcap::McapReader reader;
    const auto open_status = reader.open(path.string());
    if (!open_status.ok())
    {
        throw std::runtime_error("Failed to open telemetry MCAP: " + path.string() + ": " +
                                 open_status.message);
    }

    const auto summary_status = reader.readSummary(mcap::ReadSummaryMethod::ForceScan);
    if (!summary_status.ok())
    {
        add_event(timeline,
                  0.0,
                  {},
                  0U,
                  EventSeverity::Warning,
                  "MCAP summary scan reported: " + summary_status.message);
    }
    for (const auto &[channel_id, channel] : reader.channels())
    {
        const auto schema = reader.schema(channel->schemaId);
        if (!is_supported_channel(*channel, schema.get()))
        {
            ++timeline.diagnostics.unsupported_channels;
            ++timeline.diagnostics.schema_mismatches;
            add_event(timeline,
                      0.0,
                      {},
                      channel_id,
                      EventSeverity::Info,
                      "Unsupported MCAP channel preserved: " + channel->topic);
        }
    }

    auto view = reader.readMessages(
        [&timeline](const mcap::Status &status)
        {
            ++timeline.diagnostics.decode_failures;
            add_event(timeline,
                      0.0,
                      {},
                      0U,
                      EventSeverity::Warning,
                      "MCAP read problem: " + status.message);
        });
    for (const auto &record : view)
    {
        if (!is_supported_channel(*record.channel, record.schema.get()))
        {
            ++timeline.diagnostics.skipped_records;
            continue;
        }
        TelemetrySample sample;
        const auto data = std::span<const std::byte>(record.message.data, record.message.dataSize);
        if (decode_canonical_sample_protobuf(data, sample, timeline.diagnostics))
        {
            timeline.samples.push_back(sample);
        }
    }
    reader.close();
    finalize_timeline(timeline);
    return timeline;
}

} // namespace animus::telemetry_core
