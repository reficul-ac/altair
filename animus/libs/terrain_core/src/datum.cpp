#include "animus/terrain_core/datum.hpp"

#if defined(ANIMUS_TERRAIN_CORE_HAS_PROJ)
#include <proj.h>
#endif

#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace animus::terrain_core
{
namespace
{

#if defined(ANIMUS_TERRAIN_CORE_HAS_PROJ)
struct ProjContextDeleter
{
    void operator()(PJ_CONTEXT *context) const
    {
        proj_context_destroy(context);
    }
};

struct ProjDeleter
{
    void operator()(PJ *projection) const
    {
        proj_destroy(projection);
    }
};

using ProjContext = std::unique_ptr<PJ_CONTEXT, ProjContextDeleter>;
using ProjPipeline = std::unique_ptr<PJ, ProjDeleter>;
#endif

} // namespace

GeoidCorrectionGrid::GeoidCorrectionGrid(std::filesystem::path grid_path)
    : grid_path_(std::move(grid_path))
{
}

bool GeoidCorrectionGrid::configured() const
{
    return !grid_path_.empty();
}

const std::filesystem::path &GeoidCorrectionGrid::path() const
{
    return grid_path_;
}

DatumCorrection GeoidCorrectionGrid::ellipsoid_to_orthometric_offset(const double lat_deg,
                                                                     const double lon_deg) const
{
    if (grid_path_.empty())
    {
        return {};
    }
    if (!std::isfinite(lat_deg) || !std::isfinite(lon_deg))
    {
        throw std::invalid_argument("Datum correction coordinates must be finite");
    }

#if defined(ANIMUS_TERRAIN_CORE_HAS_PROJ)
    ProjContext context(proj_context_create());
    if (!context)
    {
        throw std::runtime_error("Failed to create PROJ context");
    }
    const std::string pipeline_definition =
        "+proj=pipeline +step +proj=vgridshift +grids=" + grid_path_.string() + " +multiplier=1";
    ProjPipeline pipeline(proj_create(context.get(), pipeline_definition.c_str()));
    if (!pipeline)
    {
        throw std::runtime_error("Failed to create PROJ vertical grid shift pipeline for " +
                                 grid_path_.string());
    }

    PJ_COORD input = proj_coord(lon_deg, lat_deg, 0.0, 0.0);
    const PJ_COORD output = proj_trans(pipeline.get(), PJ_FWD, input);
    if (!std::isfinite(output.xyz.z))
    {
        return {};
    }
    return {true, output.xyz.z};
#else
    (void)lat_deg;
    (void)lon_deg;
    throw std::runtime_error("terrain_core was built without PROJ datum correction support");
#endif
}

std::optional<double> height_above_terrain_m(const AltitudeReference reference,
                                             const double altitude_m,
                                             const double terrain_orthometric_m,
                                             const double lat_deg,
                                             const double lon_deg,
                                             const GeoidCorrectionGrid &grid)
{
    if (!std::isfinite(altitude_m) || !std::isfinite(terrain_orthometric_m))
    {
        return std::nullopt;
    }

    switch (reference)
    {
    case AltitudeReference::MslOrthometric:
        return altitude_m - terrain_orthometric_m;
    case AltitudeReference::Ellipsoid:
    {
        const DatumCorrection correction = grid.ellipsoid_to_orthometric_offset(lat_deg, lon_deg);
        if (!correction.available)
        {
            return std::nullopt;
        }
        return altitude_m + correction.correction_m - terrain_orthometric_m;
    }
    case AltitudeReference::TerrainRelative:
        return altitude_m;
    case AltitudeReference::Unknown:
        return std::nullopt;
    }
    return std::nullopt;
}

} // namespace animus::terrain_core
