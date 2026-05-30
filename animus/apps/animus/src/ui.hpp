#pragma once

#include "animus/render_core/gl_info.hpp"
#include "animus/render_core/render_stats.hpp"
#include "animus/telemetry_core/telemetry.hpp"
#include "animus/telemetry_live/live_telemetry_buffer.hpp"
#include "animus/telemetry_live/udp_mavlink_receiver.hpp"
#include "animus/terrain_core/terrain_stream.hpp"
#include "app_config.hpp"
#include "options.hpp"
#include "plot_ui.hpp"
#include "plan_visualization.hpp"
#include "report_export.hpp"
#include "telemetry_signal_catalog.hpp"
#include "timeline_review.hpp"

#include <array>
#include <cstddef>
#include <filesystem>
#include <map>
#include <optional>
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

enum class MapOrientationMode
{
    NorthUp,
    TrackUp,
    FreeRotate,
};

struct Map2DCamera
{
    float target_x = 0.0F;
    float target_z = 0.0F;
    float distance = 4.2F;
    float rotation_rad = 0.0F;
    MapOrientationMode orientation = MapOrientationMode::NorthUp;
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
    enum class TerrainConfidence
    {
        ExactResidentTile,
        FallbackResidentTile,
        SyntheticResidentTile,
        Unavailable,
        DatumUncertain,
    };
    enum class TerrainClearanceStatus
    {
        Ok,
        Caution,
        Warning,
        Unknown,
    };
    struct ForwardClearanceSample
    {
        double horizon_s = 0.0;
        double lat_deg = 0.0;
        double lon_deg = 0.0;
        std::optional<double> terrain_elevation_m;
        std::optional<double> terrain_clearance_m;
        TerrainConfidence confidence = TerrainConfidence::Unavailable;
        TerrainClearanceStatus status = TerrainClearanceStatus::Unknown;
    };
    struct SelectedEntityTerrain
    {
        std::optional<double> terrain_elevation_m;
        std::optional<double> terrain_clearance_m;
        TerrainConfidence confidence = TerrainConfidence::Unavailable;
        std::vector<ForwardClearanceSample> forward_clearance;
    };
    SelectedEntityTerrain selected_entity_terrain;
    TimelineReviewData review;
    std::optional<animus::telemetry_core::Timeline> ghost_baseline;
    std::string ghost_diagnostic;
    animus::telemetry_core::EntityId selected_entity;
    std::string live_endpoint;
    animus::telemetry_live::UdpMavlinkReceiverStats receiver_stats;
    animus::telemetry_live::LiveTelemetryBufferStats live_stats;
    MavlinkValueStore mavlink_values;
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
    struct Definition
    {
        std::string id;
        std::string name;
        std::string type;
        std::string model_status = "not loaded";
        bool model_loaded = false;
    };

    std::size_t registry_package_count = 0;
    std::string default_vehicle_id;
    std::string default_vehicle_name = "Generic RC Plane";
    std::string default_vehicle_type = "rc_plane";
    std::string model_status = "not loaded";
    bool model_loaded = false;
    std::vector<Definition> definitions;
    std::string selected_detected_type = "unknown";
    std::string selected_vehicle_id = "animus.rc_plane.generic";
    std::string selected_vehicle_name = "Generic RC Plane";
    std::string selected_vehicle_type = "rc_plane";
    std::string selected_model_status = "fallback icon";
    std::string selected_fallback_reason;
    std::string selected_heading_source = "auto";
    std::string selected_altitude_placement = "terrain_resolved";
    bool selected_model_loaded = false;
    bool selected_force_icon_only = false;
    float selected_scale = 1.0F;
    std::vector<std::string> diagnostics;
};

enum class UiNavigationMode
{
    View,
    Layers,
    Telemetry,
    Signals,
    Capture,
    Settings,
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

enum class SelectedVehicleInspectorMode
{
    Compact,
    Expanded,
};

struct UiState
{
    WorkspaceMode workspace_mode = WorkspaceMode::FlyTest;
    ViewMode view_mode = ViewMode::Terrain3D;
    FpvSettings fpv;
    std::string fpv_status = "unavailable";
    std::string fpv_selected_label = "none";
    UiNavigationMode active_mode = UiNavigationMode::View;
    InspectorTarget inspector_target = InspectorTarget::Terrain;
    bool developer_diagnostics_visible = false;
    bool telemetry_diagnostics_visible = false;
    bool mavlink_inspector_visible = true;
    bool telemetry_entity_selected = false;
    bool telemetry_tracks_visible = true;
    bool telemetry_labels_visible = true;
    std::size_t selected_entity_tail_points = 1000U;
    SelectedVehicleTestMetadata selected_vehicle_test;
    SelectedVehicleInspectorMode selected_vehicle_inspector_mode =
        SelectedVehicleInspectorMode::Compact;
    std::filesystem::path ghost_recent_baseline_path;
    bool ghost_layer_visible = false;
    std::filesystem::path report_export_default_dir = "artifacts/animus/reports";
    ReportExportUiState report_export;
    bool bathymetry_enabled = false;
    AppLayerSettings layers;
    bool follow_selected_entity = false;
    bool request_fit_all_entities = false;
    bool request_center_selected_entity = false;
    bool request_fit_selected_entity = false;
    bool request_jump_latest_sample = false;
    bool request_config_save = false;
    bool request_config_save_default = false;
    bool request_config_reload = false;
    bool request_config_reset = false;
    bool request_workspace_layout_reset = false;
    bool workspace_layout_applied = false;
    bool workspace_layout_restore_pending = false;
    bool main_panel_restore_pending = false;
    bool inspector_restore_pending = false;
    bool timeline_restore_pending = false;
    bool plot_shelf_restore_pending = false;
    bool inspector_visible = true;
    bool timeline_visible = true;
    float timeline_height_px = 144.0F;
    std::map<std::string, AppWorkspaceLayout> workspace_layouts;
    bool timeline_follow_latest = true;
    std::optional<double> request_timeline_seek_time_s;
    std::optional<std::size_t> selected_review_marker_index;
    bool request_home_view = false;
    int zoom_steps = 0;
    std::array<char, 64> telemetry_entity_filter{};
    std::array<char, 96> signal_filter{};
    std::uint32_t selected_mavlink_inspector_message_id = 0U;
    TelemetryEventFilters telemetry_event_filters;
    std::vector<TimelineBookmark> timeline_bookmarks;
    std::vector<TimelineReviewMarker> timeline_frame_time_markers;
    TimelineFrameTimeReviewState timeline_frame_time_state;
    TimelineReviewFilterState timeline_review_filters;
    TimelineFilterPreset timeline_review_filter_preset = TimelineFilterPreset::All;
    std::array<char, 96> timeline_bookmark_note{};
    int timeline_bookmark_severity = 0;
    int timeline_bookmark_category = 0;
    PlotUiState plot_ui;
};

struct PlanVisualizationState
{
    bool overlay_visible = true;
    std::array<char, 512> path{};
    std::filesystem::path loaded_path;
    std::optional<PlanVisualizationData> data;
    std::vector<std::string> diagnostics;
    std::string error;
};

[[nodiscard]] std::filesystem::path screenshot_path(const ScreenshotToolState &tool);
[[nodiscard]] std::filesystem::path recorder_output_path(const Mp4RecorderState &recorder);
void start_mp4_recording(Mp4RecorderState &recorder);
void finish_mp4_recording(Mp4RecorderState &recorder);
[[nodiscard]] bool telemetry_event_visible(const animus::telemetry_core::Event &event,
                                           const TelemetryEventFilters &filters);

void draw_app_workspace(Options &options,
                        const std::filesystem::path &pack_root,
                        const animus::render_core::GlInfo &gl_info,
                        const animus::render_core::RenderStats &stats,
                        const animus::terrain_core::TerrainStreamSnapshot &snapshot,
                        const Camera &camera,
                        Map2DCamera &map_camera,
                        int selected_zoom,
                        const std::vector<animus::terrain_core::TileRenderDecision> &visible_tiles,
                        std::size_t upload_bytes_used,
                        int texture_uploads_used,
                        int mesh_uploads_used,
                        std::size_t resident_gpu_bytes,
                        TelemetryPlaybackState &playback,
                        PlanVisualizationState &plan_state,
                        const VehicleRuntimeStatus &vehicle_status,
                        ScreenshotToolState &screenshot_tool,
                        Mp4RecorderState &mp4_recorder,
                        UiState &ui_state,
                        bool &state_colors,
                        bool &highlight_fallback,
                        bool &overlay_enabled,
                        float &overlay_opacity);

} // namespace animus::app
