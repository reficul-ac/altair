#pragma once

#include "animus/telemetry_core/telemetry.hpp"
#include "ui.hpp"

#include <optional>
#include <string>

namespace animus::app
{

struct FpvRenderParams
{
    int zoom = 12;
    int center_x = 0;
    int center_y = 0;
    float height_scale = 0.0015F;
};

struct FpvPose
{
    Vec3 eye;
    Vec3 forward;
    Vec3 up;
    float roll_rad = 0.0F;
    float pitch_rad = 0.0F;
    float heading_rad = 0.0F;
};

struct FpvCameraState
{
    std::optional<FpvPose> pose;
    std::string status = "unavailable";
};

[[nodiscard]] FpvSettings clamped_fpv_settings(FpvSettings settings);
[[nodiscard]] float local_web_mercator_meters_per_tile(double lat_deg, int zoom);
[[nodiscard]] std::optional<FpvPose> build_fpv_pose(const telemetry_core::TelemetrySample &sample,
                                                    const FpvSettings &settings,
                                                    const FpvRenderParams &params);
[[nodiscard]] std::optional<FpvPose>
build_fpv_pose_from_world(const telemetry_core::TelemetrySample &sample,
                          Vec3 world_position,
                          const FpvSettings &settings,
                          const FpvRenderParams &params);
[[nodiscard]] const char *fpv_status_label(const FpvCameraState &state);
void reset_fpv_settings(FpvSettings &settings);
void update_fpv_camera(FpvCameraState &state,
                       const std::optional<telemetry_core::TelemetrySample> &sample,
                       const FpvSettings &settings,
                       const FpvRenderParams &params,
                       double dt_s);
void update_fpv_camera_from_world(FpvCameraState &state,
                                  const std::optional<telemetry_core::TelemetrySample> &sample,
                                  std::optional<Vec3> world_position,
                                  const FpvSettings &settings,
                                  const FpvRenderParams &params,
                                  double dt_s);

} // namespace animus::app
