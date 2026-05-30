#include "fpv_camera.hpp"

#include "animus/geo_core/tile_math.hpp"

#include <algorithm>
#include <cmath>

namespace animus::app
{
namespace
{

constexpr double kPi = 3.14159265358979323846;
constexpr double kEarthRadiusM = 6378137.0;

[[nodiscard]] Vec3 add(const Vec3 a, const Vec3 b)
{
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

[[nodiscard]] Vec3 sub(const Vec3 a, const Vec3 b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

[[nodiscard]] Vec3 mul(const Vec3 a, const float scale)
{
    return {a.x * scale, a.y * scale, a.z * scale};
}

[[nodiscard]] float dot(const Vec3 a, const Vec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

[[nodiscard]] Vec3 normalize(const Vec3 value)
{
    const float length = std::sqrt(dot(value, value));
    if (length <= 0.000001F)
    {
        return {0.0F, 0.0F, -1.0F};
    }
    return {value.x / length, value.y / length, value.z / length};
}

[[nodiscard]] float heading_rad(const telemetry_core::TelemetrySample &sample)
{
    if (sample.heading_deg)
    {
        return static_cast<float>(*sample.heading_deg * kPi / 180.0);
    }
    return static_cast<float>(sample.yaw_rad.value_or(0.0));
}

[[nodiscard]] Vec3 render_axis_from_ned(const double north,
                                        const double east,
                                        const double down,
                                        const float meters_per_tile,
                                        const float height_scale)
{
    return {static_cast<float>(east / static_cast<double>(meters_per_tile)),
            static_cast<float>(-down * static_cast<double>(height_scale)),
            static_cast<float>(-north / static_cast<double>(meters_per_tile))};
}

[[nodiscard]] Vec3 lerp(const Vec3 a, const Vec3 b, const float t)
{
    return add(a, mul(sub(b, a), t));
}

[[nodiscard]] float smoothing_alpha(const float smoothing_s, const double dt_s)
{
    if (smoothing_s <= 0.0F || dt_s <= 0.0)
    {
        return 1.0F;
    }
    return std::clamp(1.0F - std::exp(static_cast<float>(-dt_s) / smoothing_s), 0.0F, 1.0F);
}

} // namespace

FpvSettings clamped_fpv_settings(FpvSettings settings)
{
    settings.fov_deg = std::clamp(settings.fov_deg, 35.0F, 110.0F);
    settings.forward_offset_m = std::clamp(settings.forward_offset_m, -1.0F, 3.0F);
    settings.height_offset_m = std::clamp(settings.height_offset_m, -1.0F, 3.0F);
    settings.smoothing_s = std::clamp(settings.smoothing_s, 0.0F, 2.0F);
    return settings;
}

float local_web_mercator_meters_per_tile(const double lat_deg, const int zoom)
{
    const double lat_rad = std::clamp(lat_deg, -85.05112878, 85.05112878) * kPi / 180.0;
    const double tiles = std::ldexp(1.0, std::clamp(zoom, 0, 30));
    return static_cast<float>((2.0 * kPi * kEarthRadiusM * std::cos(lat_rad)) / tiles);
}

std::optional<FpvPose> build_fpv_pose(const telemetry_core::TelemetrySample &sample,
                                      const FpvSettings &settings,
                                      const FpvRenderParams &params)
{
    if (!sample.fields.position)
    {
        return std::nullopt;
    }
    if (!sample.heading_deg && !sample.yaw_rad)
    {
        return std::nullopt;
    }

    const FpvSettings clamped = clamped_fpv_settings(settings);
    const float yaw = heading_rad(sample);
    const float pitch = static_cast<float>(sample.pitch_rad.value_or(0.0));
    const float roll = static_cast<float>(sample.roll_rad.value_or(0.0));
    const float meters_per_tile =
        std::max(local_web_mercator_meters_per_tile(sample.lat_deg, params.zoom), 1.0F);

    const double cp = std::cos(pitch);
    const double sp = std::sin(pitch);
    const double cr = std::cos(roll);
    const double sr = std::sin(roll);
    const double cy = std::cos(yaw);
    const double sy = std::sin(yaw);

    const Vec3 forward = normalize(
        render_axis_from_ned(cp * cy, cp * sy, -sp, meters_per_tile, params.height_scale));
    const Vec3 down = normalize(render_axis_from_ned(cr * sp * cy + sr * sy,
                                                     cr * sp * sy - sr * cy,
                                                     cr * cp,
                                                     meters_per_tile,
                                                     params.height_scale));
    const Vec3 up = normalize(mul(down, -1.0F));

    const auto coord = geo_core::lat_lon_to_tile(sample.lat_deg, sample.lon_deg, params.zoom);
    const auto uv = geo_core::lat_lon_to_tile_uv(sample.lat_deg, sample.lon_deg, coord);
    Vec3 eye{static_cast<float>(static_cast<double>(coord.x - params.center_x) + uv.u),
             0.0F,
             static_cast<float>(static_cast<double>(coord.y - params.center_y) + uv.v)};
    if (sample.altitude_msl_m)
    {
        eye.y = static_cast<float>(*sample.altitude_msl_m) * params.height_scale;
    }
    else if (sample.altitude_relative_m)
    {
        eye.y = static_cast<float>(*sample.altitude_relative_m) * params.height_scale;
    }

    eye = add(add(eye, mul(forward, clamped.forward_offset_m / meters_per_tile)),
              mul(up, clamped.height_offset_m * params.height_scale));
    return FpvPose{eye, forward, up, roll, pitch, yaw};
}

std::optional<FpvPose> build_fpv_pose_from_world(const telemetry_core::TelemetrySample &sample,
                                                 Vec3 world_position,
                                                 const FpvSettings &settings,
                                                 const FpvRenderParams &params)
{
    std::optional<FpvPose> pose = build_fpv_pose(sample, settings, params);
    if (!pose)
    {
        return std::nullopt;
    }
    const FpvSettings clamped = clamped_fpv_settings(settings);
    const float meters_per_tile =
        std::max(local_web_mercator_meters_per_tile(sample.lat_deg, params.zoom), 1.0F);
    pose->eye =
        add(add(world_position, mul(pose->forward, clamped.forward_offset_m / meters_per_tile)),
            mul(pose->up, clamped.height_offset_m * params.height_scale));
    return pose;
}

const char *fpv_status_label(const FpvCameraState &state)
{
    return state.status.c_str();
}

void reset_fpv_settings(FpvSettings &settings)
{
    settings = FpvSettings{};
}

void update_fpv_camera(FpvCameraState &state,
                       const std::optional<telemetry_core::TelemetrySample> &sample,
                       const FpvSettings &settings,
                       const FpvRenderParams &params,
                       const double dt_s)
{
    update_fpv_camera_from_world(state, sample, std::nullopt, settings, params, dt_s);
}

void update_fpv_camera_from_world(FpvCameraState &state,
                                  const std::optional<telemetry_core::TelemetrySample> &sample,
                                  const std::optional<Vec3> world_position,
                                  const FpvSettings &settings,
                                  const FpvRenderParams &params,
                                  const double dt_s)
{
    const std::optional<FpvPose> next =
        sample ? (world_position
                      ? build_fpv_pose_from_world(*sample, *world_position, settings, params)
                      : build_fpv_pose(*sample, settings, params))
               : std::nullopt;
    if (!next)
    {
        state.status = state.pose ? "held" : "unavailable";
        return;
    }
    const float alpha = smoothing_alpha(clamped_fpv_settings(settings).smoothing_s, dt_s);
    if (!state.pose || alpha >= 1.0F)
    {
        state.pose = *next;
    }
    else
    {
        state.pose->eye = lerp(state.pose->eye, next->eye, alpha);
        state.pose->forward = normalize(lerp(state.pose->forward, next->forward, alpha));
        state.pose->up = normalize(lerp(state.pose->up, next->up, alpha));
        state.pose->roll_rad =
            state.pose->roll_rad + (next->roll_rad - state.pose->roll_rad) * alpha;
        state.pose->pitch_rad =
            state.pose->pitch_rad + (next->pitch_rad - state.pose->pitch_rad) * alpha;
        state.pose->heading_rad =
            state.pose->heading_rad + (next->heading_rad - state.pose->heading_rad) * alpha;
    }
    state.status = "live";
}

} // namespace animus::app
