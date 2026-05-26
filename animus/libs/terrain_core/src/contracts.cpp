#include "animus/terrain_core/contracts.hpp"

namespace animus::terrain_core
{

std::string_view to_string(const LayerType type)
{
    switch (type)
    {
    case LayerType::Imagery:
        return "imagery";
    case LayerType::Elevation:
        return "elevation";
    case LayerType::Bathymetry:
        return "bathymetry";
    case LayerType::Overlay:
        return "overlay";
    }

    return "unknown";
}

std::string_view to_string(const RasterFormat format)
{
    switch (format)
    {
    case RasterFormat::Float32:
        return "float32";
    case RasterFormat::UInt8RGB:
        return "uint8_rgb";
    case RasterFormat::UInt8RGBA:
        return "uint8_rgba";
    }

    return "unknown";
}

std::string_view to_string(const SamplingMode mode)
{
    switch (mode)
    {
    case SamplingMode::Center:
        return "center";
    case SamplingMode::Corner:
        return "corner";
    }

    return "unknown";
}

std::string_view to_string(const AltitudeDatum datum)
{
    switch (datum)
    {
    case AltitudeDatum::Unknown:
        return "unknown";
    case AltitudeDatum::MslOrthometric:
        return "msl_orthometric";
    case AltitudeDatum::Ellipsoid:
        return "ellipsoid";
    case AltitudeDatum::TerrainRelative:
        return "terrain_relative";
    }

    return "unknown";
}

std::string_view to_string(const TileState state)
{
    switch (state)
    {
    case TileState::Missing:
        return "missing";
    case TileState::Queued:
        return "queued";
    case TileState::Loading:
        return "loading";
    case TileState::Decoding:
        return "decoding";
    case TileState::Decoded:
        return "decoded";
    case TileState::BuildingMesh:
        return "building_mesh";
    case TileState::ReadyCpu:
        return "ready_cpu";
    case TileState::UploadQueued:
        return "upload_queued";
    case TileState::ReadyGpu:
        return "ready_gpu";
    case TileState::Visible:
        return "visible";
    case TileState::Failed:
        return "failed";
    case TileState::UsingFallback:
        return "using_fallback";
    case TileState::Retiring:
        return "retiring";
    }

    return "unknown";
}

} // namespace animus::terrain_core
