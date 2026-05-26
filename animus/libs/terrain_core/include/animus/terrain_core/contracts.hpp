#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace animus::terrain_core {

enum class LayerType {
    Imagery,
    Elevation,
    Bathymetry,
    Overlay,
};

enum class RasterFormat {
    Float32,
    UInt8RGB,
    UInt8RGBA,
};

enum class SamplingMode {
    Center,
    Corner,
};

enum class TileState {
    Missing,
    Queued,
    Loading,
    Decoding,
    Decoded,
    BuildingMesh,
    ReadyCpu,
    UploadQueued,
    ReadyGpu,
    Visible,
    Failed,
    UsingFallback,
    Retiring,
};

struct LayerSpec {
    LayerType type = LayerType::Imagery;
    std::string source;
    std::string style;
    std::string extra;
    int resolution = 256;
    int min_zoom = 0;
    int max_zoom = 18;
};

struct Raster {
    int width = 0;
    int height = 0;
    int channels = 0;
    RasterFormat format = RasterFormat::UInt8RGBA;
    SamplingMode sampling_mode = SamplingMode::Center;
    std::vector<float> float_data;
    std::vector<std::uint8_t> byte_data;
    std::optional<float> no_data_value;
};

std::string_view to_string(LayerType type);
std::string_view to_string(RasterFormat format);
std::string_view to_string(SamplingMode mode);
std::string_view to_string(TileState state);

} // namespace animus::terrain_core
