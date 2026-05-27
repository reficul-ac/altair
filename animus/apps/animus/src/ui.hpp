#pragma once

#include "animus/render_core/gl_info.hpp"
#include "animus/render_core/render_stats.hpp"
#include "animus/telemetry_core/telemetry.hpp"
#include "animus/telemetry_live/live_telemetry_buffer.hpp"
#include "animus/telemetry_live/udp_mavlink_receiver.hpp"
#include "animus/terrain_core/terrain_stream.hpp"
#include "options.hpp"

#include <array>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace animus::app
{

struct Vec3
{
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

struct Camera
{
    Vec3 target{0.0F, 3.45F, 0.0F};
    float distance = 4.2F;
    float yaw = -0.72F;
    float pitch = 0.72F;
};

struct ScreenshotToolState
{
    std::array<char, 512> png_path{};
    bool pending_png = false;
    std::string status = "ready";
};

struct Mp4RecorderState
{
    std::array<char, 512> mp4_path{};
    int fps = 30;
    bool recording = false;
    bool pending_stop = false;
    int frame_count = 0;
    std::filesystem::path sequence_dir;
    std::string status = "ready";
};

struct TelemetryPlaybackState
{
    animus::telemetry_core::Timeline timeline;
    animus::telemetry_core::PlaybackClock clock;
    bool loaded = false;
    bool live = false;
    bool terrain_height_unavailable = false;
    bool unknown_datum_relative_fallback = false;
    bool geoid_correction_unavailable = false;
    animus::telemetry_core::EntityId selected_entity;
    std::string live_endpoint;
    animus::telemetry_live::UdpMavlinkReceiverStats receiver_stats;
    animus::telemetry_live::LiveTelemetryBufferStats live_stats;
    double live_snapshot_elapsed_s = 0.0;
    double live_ingest_ms = 0.0;
    double live_prune_finalize_ms = 0.0;
    double live_snapshot_copy_ms = 0.0;
    double live_overlay_draw_ms = 0.0;
    std::size_t live_rendered_trail_points = 0;
    std::size_t live_frame_batch_messages = 0;
    std::size_t live_frame_batch_samples = 0;
};

struct VehicleRuntimeStatus
{
    std::size_t registry_package_count = 0;
    std::string default_vehicle_id;
    std::string default_vehicle_name = "Generic RC Plane";
    std::string default_vehicle_type = "rc_plane";
    std::string model_status = "not loaded";
    bool model_loaded = false;
    std::vector<std::string> diagnostics;
};

enum class UiNavigationMode
{
    View,
    Layers,
    Telemetry,
    Capture,
    Developer,
};

enum class InspectorTarget
{
    None,
    Terrain,
    Layer,
    TelemetrySource,
    Entity,
};

struct TelemetryEventFilters
{
    bool show_info = false;
    bool show_warnings = true;
    bool show_errors = true;
};

struct UiState
{
    WorkspaceMode workspace_mode = WorkspaceMode::Operator;
    UiNavigationMode active_mode = UiNavigationMode::View;
    InspectorTarget inspector_target = InspectorTarget::Terrain;
    bool developer_diagnostics_visible = false;
    bool telemetry_diagnostics_visible = false;
    bool telemetry_entity_selected = false;
    bool follow_selected_entity = false;
    std::array<char, 64> telemetry_entity_filter{};
    TelemetryEventFilters telemetry_event_filters;
};

[[nodiscard]] std::filesystem::path screenshot_path(const ScreenshotToolState &tool);
[[nodiscard]] std::filesystem::path recorder_output_path(const Mp4RecorderState &recorder);
void start_mp4_recording(Mp4RecorderState &recorder);
void finish_mp4_recording(Mp4RecorderState &recorder);
[[nodiscard]] bool telemetry_event_visible(const animus::telemetry_core::Event &event,
                                           const TelemetryEventFilters &filters);

void draw_app_workspace(const Options &options,
                        const std::filesystem::path &pack_root,
                        const animus::render_core::GlInfo &gl_info,
                        const animus::render_core::RenderStats &stats,
                        const animus::terrain_core::TerrainStreamSnapshot &snapshot,
                        const Camera &camera,
                        int selected_zoom,
                        const std::vector<animus::terrain_core::TileRenderDecision> &visible_tiles,
                        std::size_t upload_bytes_used,
                        int texture_uploads_used,
                        int mesh_uploads_used,
                        std::size_t resident_gpu_bytes,
                        TelemetryPlaybackState &playback,
                        const VehicleRuntimeStatus &vehicle_status,
                        ScreenshotToolState &screenshot_tool,
                        Mp4RecorderState &mp4_recorder,
                        UiState &ui_state,
                        bool &state_colors,
                        bool &highlight_fallback,
                        bool &overlay_enabled,
                        float &overlay_opacity);

} // namespace animus::app
