#pragma once

#include <filesystem>
#include <optional>

namespace animus::terrain_core
{

enum class AltitudeReference
{
    Unknown,
    MslOrthometric,
    Ellipsoid,
    TerrainRelative,
};

struct DatumCorrection
{
    bool available = false;
    double correction_m = 0.0;
};

class GeoidCorrectionGrid
{
  public:
    GeoidCorrectionGrid() = default;
    explicit GeoidCorrectionGrid(std::filesystem::path grid_path);

    [[nodiscard]] bool configured() const;
    [[nodiscard]] const std::filesystem::path &path() const;
    [[nodiscard]] DatumCorrection ellipsoid_to_orthometric_offset(double lat_deg,
                                                                  double lon_deg) const;

  private:
    std::filesystem::path grid_path_;
};

[[nodiscard]] std::optional<double> height_above_terrain_m(AltitudeReference reference,
                                                           double altitude_m,
                                                           double terrain_orthometric_m,
                                                           double lat_deg,
                                                           double lon_deg,
                                                           const GeoidCorrectionGrid &grid);

} // namespace animus::terrain_core
