#include "animus/telemetry_core/telemetry.hpp"

#include "animus/telemetry_core/mavlink.hpp"
#include "animus/telemetry_core/mavlink_reducer.hpp"

#include <fstream>
#include <stdexcept>

namespace animus::telemetry_core
{

Timeline load_tlog_bytes(const std::vector<std::uint8_t> &bytes)
{
    MavlinkParseResult parsed = parse_mavlink_stream(bytes);
    Timeline timeline = reduce_mavlink_messages(parsed.messages, parsed.diagnostics);
    timeline.source_format = TelemetryImportFormat::Tlog;
    return timeline;
}

Timeline load_tlog(const std::filesystem::path &path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        throw std::runtime_error("Failed to open telemetry tlog: " + path.string());
    }
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),
                                    std::istreambuf_iterator<char>());
    return load_tlog_bytes(bytes);
}

} // namespace animus::telemetry_core
