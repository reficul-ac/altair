#pragma once

#include "animus/telemetry_core/telemetry.hpp"

#include <cstddef>
#include <span>

namespace animus::telemetry_core
{

[[nodiscard]] bool decode_canonical_sample_protobuf(std::span<const std::byte> bytes,
                                                    TelemetrySample &sample,
                                                    ParserDiagnostics &diagnostics);
[[nodiscard]] const char *canonical_protobuf_schema_name();

} // namespace animus::telemetry_core
