#include "animus/telemetry_core/telemetry.hpp"

#include <stdexcept>

namespace animus::telemetry_core
{

Timeline load_mcap_protobuf(const std::filesystem::path &)
{
    throw std::runtime_error("Animus telemetry imports were disabled at configure time");
}

Timeline load_hdf5(const std::filesystem::path &)
{
    throw std::runtime_error("Animus telemetry imports were disabled at configure time");
}

} // namespace animus::telemetry_core
