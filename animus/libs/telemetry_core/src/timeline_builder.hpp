#pragma once

#include "animus/telemetry_core/telemetry.hpp"

namespace animus::telemetry_core
{

[[nodiscard]] bool entity_less(EntityId a, EntityId b);
void add_event(Timeline &timeline,
               double time_s,
               EntityId id,
               std::uint32_t message_id,
               EventSeverity severity,
               std::string message);
void finalize_timeline(Timeline &timeline);

} // namespace animus::telemetry_core
