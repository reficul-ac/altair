#include "animus/telemetry_core/telemetry.hpp"

#include <stdexcept>

namespace animus::telemetry_core
{

const char *to_string(const TelemetryImportFormat format)
{
    switch (format)
    {
    case TelemetryImportFormat::Tlog:
        return "tlog";
    case TelemetryImportFormat::McapProtobuf:
        return "mcap";
    case TelemetryImportFormat::Hdf5Animus:
        return "hdf5";
    }
    return "unknown";
}

Timeline load_telemetry(const std::filesystem::path &path, const TelemetryImportFormat format)
{
    switch (format)
    {
    case TelemetryImportFormat::Tlog:
        return load_tlog(path);
    case TelemetryImportFormat::McapProtobuf:
        return load_mcap_protobuf(path);
    case TelemetryImportFormat::Hdf5Animus:
        return load_hdf5(path);
    }
    throw std::invalid_argument("Unsupported telemetry import format");
}

} // namespace animus::telemetry_core
