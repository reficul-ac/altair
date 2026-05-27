#include "ui.hpp"

#include "capture.hpp"

#include "animus/terrain_core/contracts.hpp"
#include "animus/terrain_core/terrain_cache.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstdio>

namespace animus::app
{
namespace
{

const char *mode_label(const UiNavigationMode mode)
{
    switch (mode)
    {
    case UiNavigationMode::View:
        return "View";
    case UiNavigationMode::Layers:
        return "Layers";
    case UiNavigationMode::Telemetry:
        return "Telemetry";
    case UiNavigationMode::Capture:
        return "Capture";
    case UiNavigationMode::Developer:
        return "Developer";
    }
    return "View";
}

void nav_button(UiState &ui_state, UiNavigationMode mode)
{
    const bool selected = ui_state.active_mode == mode;
    if (selected)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20F, 0.38F, 0.46F, 1.0F));
    }
    if (ImGui::Button(mode_label(mode), ImVec2(-1.0F, 0.0F)))
    {
        ui_state.active_mode = mode;
        if (mode == UiNavigationMode::Developer)
        {
            ui_state.developer_diagnostics_visible = true;
        }
    }
    if (selected)
    {
        ImGui::PopStyleColor();
    }
}

const char *telemetry_state_label(const TelemetryPlaybackState &playback)
{
    if (!playback.loaded)
    {
        return "telemetry idle";
    }
    if (!playback.live)
    {
        return playback.clock.paused() ? "playback paused" : "playback running";
    }
    if (!playback.receiver_stats.connected)
    {
        return "live waiting";
    }
    return playback.receiver_stats.stale ? "live stale" : "live connected";
}

void draw_top_status_bar(const Options &options,
                         const animus::render_core::RenderStats &stats,
                         const animus::terrain_core::TerrainStreamSnapshot &snapshot,
                         const TelemetryPlaybackState &playback,
                         const Mp4RecorderState &recorder)
{
    ImGui::SetNextWindowPos(ImVec2(0.0F, 0.0F), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetIO().DisplaySize.x, 34.0F), ImGuiCond_Always);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize;
    ImGui::Begin("Animus Status", nullptr, flags);
    ImGui::TextUnformatted("Animus");
    ImGui::SameLine();
    ImGui::Text("%s", telemetry_state_label(playback));
    ImGui::SameLine();
    ImGui::Text("tiles %zu/%zu", snapshot.resident_gpu_tiles, snapshot.tiles.size());
    ImGui::SameLine();
    ImGui::Text("frame %.2f ms", stats.last_frame_seconds() * 1000.0);
    ImGui::SameLine();
    ImGui::Text("source %s",
                options.telemetry_live_udp_enabled
                    ? "Live UDP"
                    : (!options.telemetry.empty() ? "Log" : "Terrain"));
    if (recorder.recording)
    {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0F, 0.34F, 0.30F, 1.0F), "recording %d", recorder.frame_count);
    }
    ImGui::End();
}

void draw_nav(UiState &ui_state)
{
    ImGui::SetNextWindowPos(ImVec2(12.0F, 46.0F), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(116.0F, 214.0F), ImGuiCond_Always);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin("Navigate", nullptr, flags);
    nav_button(ui_state, UiNavigationMode::View);
    nav_button(ui_state, UiNavigationMode::Layers);
    nav_button(ui_state, UiNavigationMode::Telemetry);
    nav_button(ui_state, UiNavigationMode::Capture);
    nav_button(ui_state, UiNavigationMode::Developer);
    ImGui::End();
}

void draw_view_panel(const Options &options,
                     const std::filesystem::path &pack_root,
                     const Camera &camera,
                     int selected_zoom,
                     bool &state_colors,
                     bool &highlight_fallback)
{
    ImGui::Text("zoom %d  range %d..%d", selected_zoom, options.min_z, options.max_z);
    ImGui::Text("camera %.2f %.2f %.2f  distance %.2f",
                camera.target.x,
                camera.target.y,
                camera.target.z,
                camera.distance);
    ImGui::Separator();
    ImGui::Text("pack");
    ImGui::TextWrapped("%s", pack_root.string().c_str());
    ImGui::Text(
        "center %d/%d  height %.6f", options.center_x, options.center_y, options.height_scale);
    ImGui::Checkbox("State colors", &state_colors);
    ImGui::SameLine();
    ImGui::Checkbox("Fallback highlight", &highlight_fallback);
}

void draw_layer_panel(const Options &options, bool &overlay_enabled, float &overlay_opacity)
{
    ImGui::Checkbox("Overlay enabled", &overlay_enabled);
    ImGui::SliderFloat("Overlay opacity", &overlay_opacity, 0.0F, 1.0F, "%.2f");
    ImGui::Text("overlay order %d", options.overlay_order);
    ImGui::Text("overlay layers %zu", options.overlays.size());
    ImGui::Text("bathymetry %s", options.use_bathymetry ? "enabled" : "disabled");
    ImGui::Separator();
    ImGui::Text(
        "elevation GeoTIFF %s",
        options.elevation_geotiff.empty()
            ? "not set"
            : (std::filesystem::exists(options.elevation_geotiff) ? "available" : "missing"));
    ImGui::Text(
        "bathymetry GeoTIFF %s",
        options.bathymetry_geotiff.empty()
            ? "not set"
            : (std::filesystem::exists(options.bathymetry_geotiff) ? "available" : "missing"));
    ImGui::Text("imagery MBTiles %s",
                options.imagery_mbtiles.empty()
                    ? "not set"
                    : (std::filesystem::exists(options.imagery_mbtiles) ? "available" : "missing"));
    ImGui::Text("remote imagery %s",
                options.remote_imagery_url_template.empty() ? "not set" : "configured");
}

void draw_timeline_controls(TelemetryPlaybackState &playback)
{
    bool paused = playback.clock.paused();
    if (ImGui::Checkbox("Paused", &paused))
    {
        playback.clock.set_paused(paused);
    }
    ImGui::SameLine();
    bool looping = playback.clock.looping();
    if (ImGui::Checkbox("Loop", &looping))
    {
        playback.clock.set_looping(looping);
    }
    float rate = static_cast<float>(playback.clock.rate());
    if (ImGui::SliderFloat("Rate", &rate, 0.1F, 16.0F, "%.2fx"))
    {
        playback.clock.set_rate(rate);
    }
    double current_time = playback.clock.time_s();
    if (ImGui::SliderScalar("Time",
                            ImGuiDataType_Double,
                            &current_time,
                            &playback.timeline.start_time_s,
                            &playback.timeline.end_time_s,
                            "%.3f s"))
    {
        playback.clock.seek(current_time);
    }
}

void draw_entity_selector(TelemetryPlaybackState &playback)
{
    if (playback.timeline.entities.empty())
    {
        return;
    }
    int selected_index = 0;
    for (std::size_t index = 0U; index < playback.timeline.entities.size(); ++index)
    {
        if (playback.timeline.entities[index].id == playback.selected_entity)
        {
            selected_index = static_cast<int>(index);
        }
    }
    const std::string current = std::to_string(playback.selected_entity.system_id) + ":" +
                                std::to_string(playback.selected_entity.component_id);
    if (ImGui::BeginCombo("Entity", current.c_str()))
    {
        for (std::size_t index = 0U; index < playback.timeline.entities.size(); ++index)
        {
            const auto id = playback.timeline.entities[index].id;
            const std::string label =
                std::to_string(id.system_id) + ":" + std::to_string(id.component_id);
            const bool selected = static_cast<int>(index) == selected_index;
            if (ImGui::Selectable(label.c_str(), selected))
            {
                playback.selected_entity = id;
            }
            if (selected)
            {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
}

void draw_telemetry_panel(const Options &options, TelemetryPlaybackState &playback, UiState &ui)
{
    if (!playback.loaded)
    {
        ImGui::TextUnformatted("No telemetry source is active.");
        return;
    }

    if (playback.live)
    {
        ImGui::Text("Live UDP");
        ImGui::TextWrapped("%s", playback.live_endpoint.c_str());
        ImGui::Text("state %s",
                    playback.receiver_stats.connected
                        ? (playback.receiver_stats.stale ? "stale" : "connected")
                        : "waiting");
        ImGui::Text("datagrams %llu  age %.3f s",
                    static_cast<unsigned long long>(playback.receiver_stats.datagrams),
                    playback.receiver_stats.last_packet_age_s);
    }
    else
    {
        ImGui::Text("format %s",
                    animus::telemetry_core::to_string(playback.timeline.source_format));
        ImGui::TextWrapped("%s", options.telemetry.string().c_str());
        draw_timeline_controls(playback);
    }
    ImGui::Separator();
    draw_entity_selector(playback);
    ImGui::Text("entities %zu  samples %zu  events %zu",
                playback.timeline.entities.size(),
                playback.timeline.samples.size(),
                playback.timeline.events.size());
    ImGui::Checkbox("Info events", &ui.telemetry_event_filters.show_info);
    ImGui::SameLine();
    ImGui::Checkbox("Warnings", &ui.telemetry_event_filters.show_warnings);
    ImGui::SameLine();
    ImGui::Checkbox("Errors", &ui.telemetry_event_filters.show_errors);
    ImGui::Text("terrain height %s",
                playback.terrain_height_unavailable ? "unavailable for some samples" : "available");
    if (playback.unknown_datum_relative_fallback)
    {
        ImGui::TextWrapped("warning: relative altitude used with unknown altitude datum");
    }
    if (playback.geoid_correction_unavailable)
    {
        ImGui::TextWrapped("warning: ellipsoid altitude datum needs a configured geoid grid");
    }

    if (!playback.timeline.events.empty())
    {
        ImGui::Separator();
        if (ImGui::BeginTable("events", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders))
        {
            ImGui::TableSetupColumn("time");
            ImGui::TableSetupColumn("entity");
            ImGui::TableSetupColumn("msg");
            ImGui::TableSetupColumn("event");
            ImGui::TableHeadersRow();
            for (const auto &event : playback.timeline.events)
            {
                if (!telemetry_event_visible(event, ui.telemetry_event_filters))
                {
                    continue;
                }
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%.3f", event.time_s);
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%u:%u",
                            static_cast<unsigned>(event.entity_id.system_id),
                            static_cast<unsigned>(event.entity_id.component_id));
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%u", event.message_id);
                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted(event.message.c_str());
            }
            ImGui::EndTable();
        }
    }
}

void draw_capture_panel(ScreenshotToolState &screenshot_tool, Mp4RecorderState &mp4_recorder)
{
    ImGui::InputText("PNG path", screenshot_tool.png_path.data(), screenshot_tool.png_path.size());
    if (ImGui::Button("Save PNG"))
    {
        screenshot_tool.pending_png = true;
        screenshot_tool.status = "saving after this frame";
    }
    ImGui::SameLine();
    if (ImGui::Button("Use default"))
    {
        constexpr const char *default_path = "artifacts/animus/screenshots/manual_screenshot.png";
        std::snprintf(
            screenshot_tool.png_path.data(), screenshot_tool.png_path.size(), "%s", default_path);
    }
    ImGui::TextWrapped("%s", screenshot_tool.status.c_str());
    ImGui::Separator();
    ImGui::InputText("MP4 path", mp4_recorder.mp4_path.data(), mp4_recorder.mp4_path.size());
    ImGui::InputInt("FPS", &mp4_recorder.fps);
    mp4_recorder.fps = std::clamp(mp4_recorder.fps, 1, 240);
    bool record = mp4_recorder.recording;
    if (ImGui::Checkbox("Record MP4", &record))
    {
        try
        {
            if (record)
            {
                start_mp4_recording(mp4_recorder);
            }
            else
            {
                mp4_recorder.pending_stop = true;
                mp4_recorder.status = "finishing recording";
            }
        }
        catch (const std::exception &error)
        {
            mp4_recorder.recording = false;
            mp4_recorder.pending_stop = false;
            mp4_recorder.status = std::string("recording failed: ") + error.what();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Use video default"))
    {
        constexpr const char *default_path = "artifacts/animus/videos/manual_recording.mp4";
        std::snprintf(
            mp4_recorder.mp4_path.data(), mp4_recorder.mp4_path.size(), "%s", default_path);
    }
    ImGui::Text("recorded frames %d", mp4_recorder.frame_count);
    ImGui::TextWrapped("%s", mp4_recorder.status.c_str());
}

void draw_developer_panel(
    const Options &options,
    const animus::render_core::GlInfo &gl_info,
    const animus::render_core::RenderStats &stats,
    const animus::terrain_core::TerrainStreamSnapshot &snapshot,
    const std::vector<animus::terrain_core::TileRenderDecision> &visible_tiles,
    std::size_t upload_bytes_used,
    int texture_uploads_used,
    int mesh_uploads_used,
    std::size_t resident_gpu_bytes,
    const TelemetryPlaybackState &playback,
    UiState &ui)
{
    ImGui::Checkbox("Show diagnostics", &ui.developer_diagnostics_visible);
    if (!ui.developer_diagnostics_visible)
    {
        ImGui::TextUnformatted("Developer diagnostics are hidden.");
        return;
    }

    if (ImGui::CollapsingHeader("Render", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Text("visible %zu resident %zu queued %zu loading %zu ready-cpu %zu failed %zu",
                    visible_tiles.size(),
                    snapshot.resident_gpu_tiles,
                    snapshot.queued_jobs,
                    snapshot.loading_jobs,
                    snapshot.ready_cpu_tiles,
                    snapshot.failed_tiles);
        ImGui::Text("uploads textures %d meshes %d bytes %.2f MiB resident %.2f MiB",
                    texture_uploads_used,
                    mesh_uploads_used,
                    static_cast<double>(upload_bytes_used) / (1024.0 * 1024.0),
                    static_cast<double>(resident_gpu_bytes) / (1024.0 * 1024.0));
        ImGui::Text("frames %d last %.3f ms total %.3f s",
                    stats.frame_count(),
                    stats.last_frame_seconds() * 1000.0,
                    stats.total_seconds());
        ImGui::Text("GL vendor %s", gl_info.vendor.c_str());
        ImGui::Text("GL renderer %s", gl_info.renderer.c_str());
        ImGui::Text("GL version %s", gl_info.version.c_str());
    }

    if (ImGui::CollapsingHeader("Cache", ImGuiTreeNodeFlags_DefaultOpen))
    {
        const auto &cache = snapshot.cache_stats;
        ImGui::Text("L0 gpu tiles %zu bytes %.2f MiB",
                    snapshot.resident_gpu_tiles,
                    static_cast<double>(snapshot.resident_gpu_bytes) / (1024.0 * 1024.0));
        ImGui::Text("L1 prepared %zu %.2f/%.2f MiB hits %llu misses %llu evict %llu",
                    cache.l1_prepared.entries,
                    static_cast<double>(cache.l1_prepared.bytes) / (1024.0 * 1024.0),
                    static_cast<double>(cache.l1_prepared.byte_limit) / (1024.0 * 1024.0),
                    static_cast<unsigned long long>(cache.l1_prepared.counters.hits),
                    static_cast<unsigned long long>(cache.l1_prepared.counters.misses),
                    static_cast<unsigned long long>(cache.l1_prepared.counters.evictions));
        ImGui::Text("L2 rasters %zu %.2f/%.2f MiB hits %llu misses %llu evict %llu",
                    cache.l2_raster.entries,
                    static_cast<double>(cache.l2_raster.bytes) / (1024.0 * 1024.0),
                    static_cast<double>(cache.l2_raster.byte_limit) / (1024.0 * 1024.0),
                    static_cast<unsigned long long>(cache.l2_raster.counters.hits),
                    static_cast<unsigned long long>(cache.l2_raster.counters.misses),
                    static_cast<unsigned long long>(cache.l2_raster.counters.evictions));
        ImGui::Text(
            "L3 hits %llu misses %llu stores %llu synth %llu persisted %llu geotiff-fail %llu",
            static_cast<unsigned long long>(cache.l3_disk.counters.hits),
            static_cast<unsigned long long>(cache.l3_disk.counters.misses),
            static_cast<unsigned long long>(cache.l3_disk.counters.stores),
            static_cast<unsigned long long>(cache.synthesized_tiles),
            static_cast<unsigned long long>(cache.persisted_tiles),
            static_cast<unsigned long long>(cache.geotiff_extraction_failures));
    }

    if (ImGui::CollapsingHeader("Telemetry Parser"))
    {
        const auto &diag = playback.timeline.diagnostics;
        ImGui::Text("frames %llu unsupported %llu crc %llu truncated %llu",
                    static_cast<unsigned long long>(diag.frames_decoded),
                    static_cast<unsigned long long>(diag.unsupported_messages),
                    static_cast<unsigned long long>(diag.crc_failures),
                    static_cast<unsigned long long>(diag.truncated_frames));
        ImGui::Text("signed-v2 %llu bad-version %llu malformed %llu",
                    static_cast<unsigned long long>(diag.signed_v2_frames),
                    static_cast<unsigned long long>(diag.unsupported_versions),
                    static_cast<unsigned long long>(diag.malformed_frames));
        ImGui::Text("schema %llu channels %llu layouts %llu decoded-fail %llu",
                    static_cast<unsigned long long>(diag.schema_mismatches),
                    static_cast<unsigned long long>(diag.unsupported_channels),
                    static_cast<unsigned long long>(diag.unsupported_layouts),
                    static_cast<unsigned long long>(diag.decode_failures));
        ImGui::Text("skipped %llu non-monotonic %llu missing-required %llu",
                    static_cast<unsigned long long>(diag.skipped_records),
                    static_cast<unsigned long long>(diag.non_monotonic_timestamps),
                    static_cast<unsigned long long>(diag.missing_required_fields));
        if (playback.live)
        {
            ImGui::Text("queue %zu high %zu drained %zu before %zu",
                        playback.receiver_stats.queued_datagrams,
                        playback.receiver_stats.queue_high_water,
                        playback.receiver_stats.last_drain_datagrams,
                        playback.receiver_stats.last_drain_queue_before);
            ImGui::Text("dropped datagrams %llu samples %llu",
                        static_cast<unsigned long long>(playback.receiver_stats.dropped_datagrams),
                        static_cast<unsigned long long>(playback.live_stats.dropped_samples));
            ImGui::Text("live ms ingest %.3f prune/finalize %.3f copy %.3f overlay %.3f",
                        playback.live_ingest_ms,
                        playback.live_prune_finalize_ms,
                        playback.live_snapshot_copy_ms,
                        playback.live_overlay_draw_ms);
        }
    }

    if (ImGui::CollapsingHeader("Tiles"))
    {
        if (ImGui::BeginTable("tiles", 10, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders))
        {
            ImGui::TableSetupColumn("coord");
            ImGui::TableSetupColumn("state");
            ImGui::TableSetupColumn("tier");
            ImGui::TableSetupColumn("source");
            ImGui::TableSetupColumn("synth");
            ImGui::TableSetupColumn("priority");
            ImGui::TableSetupColumn("parent");
            ImGui::TableSetupColumn("age");
            ImGui::TableSetupColumn("height");
            ImGui::TableSetupColumn("error");
            ImGui::TableHeadersRow();
            for (const auto &tile : snapshot.tiles)
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%d/%d/%d", tile.coord.z, tile.coord.x, tile.coord.y);
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(
                    std::string(animus::terrain_core::to_string(tile.state)).c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(
                    std::string(animus::terrain_core::to_string(tile.cache_tier)).c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted(
                    std::string(animus::terrain_core::to_string(tile.source_type)).c_str());
                ImGui::TableSetColumnIndex(4);
                ImGui::Text("%s/%d", tile.synthetic ? "yes" : "no", tile.synthesis_depth);
                ImGui::TableSetColumnIndex(5);
                ImGui::Text("%.2f", tile.priority);
                ImGui::TableSetColumnIndex(6);
                if (tile.parent)
                {
                    ImGui::Text("%d/%d/%d", tile.parent->z, tile.parent->x, tile.parent->y);
                }
                ImGui::TableSetColumnIndex(7);
                ImGui::Text("%llu",
                            static_cast<unsigned long long>(snapshot.frame - tile.state_frame));
                ImGui::TableSetColumnIndex(8);
                ImGui::Text("%.1f..%.1f", tile.min_height_m, tile.max_height_m);
                ImGui::TableSetColumnIndex(9);
                ImGui::TextUnformatted(tile.error.c_str());
            }
            ImGui::EndTable();
        }
    }

    (void)options;
}

void draw_inspector(TelemetryPlaybackState &playback, UiState &ui_state)
{
    if (!playback.loaded || ui_state.inspector_target == InspectorTarget::None ||
        ImGui::GetIO().DisplaySize.x < 980.0F)
    {
        return;
    }
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 304.0F, 46.0F), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(292.0F, 230.0F), ImGuiCond_Always);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin("Inspector", nullptr, flags);
    if (ui_state.inspector_target == InspectorTarget::Entity)
    {
        const auto sample =
            playback.timeline.sample_at(playback.selected_entity, playback.clock.time_s());
        draw_entity_selector(playback);
        if (sample)
        {
            ImGui::Text("lat/lon %.7f %.7f", sample->lat_deg, sample->lon_deg);
            ImGui::Text(
                "alt msl %s  rel %s",
                sample->altitude_msl_m ? std::to_string(*sample->altitude_msl_m).c_str() : "n/a",
                sample->altitude_relative_m ? std::to_string(*sample->altitude_relative_m).c_str()
                                            : "n/a");
            ImGui::Text("att roll %s pitch %s yaw %s",
                        sample->roll_rad ? std::to_string(*sample->roll_rad).c_str() : "n/a",
                        sample->pitch_rad ? std::to_string(*sample->pitch_rad).c_str() : "n/a",
                        sample->yaw_rad ? std::to_string(*sample->yaw_rad).c_str() : "n/a");
            ImGui::Text("speed %s heading %s",
                        sample->ground_speed_mps ? std::to_string(*sample->ground_speed_mps).c_str()
                                                 : "n/a",
                        sample->heading_deg ? std::to_string(*sample->heading_deg).c_str() : "n/a");
        }
    }
    else if (ui_state.inspector_target == InspectorTarget::TelemetrySource)
    {
        ImGui::Text("source %s", playback.live ? "Live UDP" : "Playback");
        ImGui::Text("samples %zu", playback.timeline.samples.size());
        ImGui::Text("events %zu", playback.timeline.events.size());
    }
    else
    {
        ImGui::TextUnformatted("Terrain view");
    }
    ImGui::End();
}

void draw_bottom_timeline(TelemetryPlaybackState &playback)
{
    if (!playback.loaded || playback.live)
    {
        return;
    }
    const float available_width = ImGui::GetIO().DisplaySize.x - 448.0F;
    if (available_width < 260.0F)
    {
        return;
    }
    ImGui::SetNextWindowPos(ImVec2(144.0F, ImGui::GetIO().DisplaySize.y - 88.0F), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(available_width, 76.0F), ImGuiCond_Always);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize;
    ImGui::Begin("Timeline", nullptr, flags);
    draw_timeline_controls(playback);
    ImGui::End();
}

} // namespace

std::filesystem::path screenshot_path(const ScreenshotToolState &tool)
{
    const std::string path(tool.png_path.data());
    if (path.empty())
    {
        return "artifacts/animus/screenshots/manual_screenshot.png";
    }
    return path;
}

std::filesystem::path recorder_output_path(const Mp4RecorderState &recorder)
{
    const std::string path(recorder.mp4_path.data());
    if (path.empty())
    {
        return "artifacts/animus/videos/manual_recording.mp4";
    }
    return path;
}

std::filesystem::path recorder_sequence_dir(const std::filesystem::path &output_path)
{
    const std::filesystem::path parent =
        output_path.has_parent_path() ? output_path.parent_path() : std::filesystem::path(".");
    const std::string stem =
        output_path.stem().empty() ? "manual_recording" : output_path.stem().string();
    return parent / (stem + "_frames");
}

void start_mp4_recording(Mp4RecorderState &recorder)
{
    const std::filesystem::path output_path = recorder_output_path(recorder);
    recorder.sequence_dir = recorder_sequence_dir(output_path);
    if (std::filesystem::exists(recorder.sequence_dir))
    {
        std::filesystem::remove_all(recorder.sequence_dir);
    }
    std::filesystem::create_directories(recorder.sequence_dir);
    recorder.frame_count = 0;
    recorder.pending_stop = false;
    recorder.recording = true;
    recorder.status = "recording to " + output_path.string();
}

void finish_mp4_recording(Mp4RecorderState &recorder)
{
    const std::filesystem::path output_path = recorder_output_path(recorder);
    if (recorder.frame_count <= 0)
    {
        recorder.status = "recording stopped with no frames";
        recorder.pending_stop = false;
        recorder.recording = false;
        return;
    }
    recorder.status = "encoding " + output_path.string();
    animus::app::encode_mp4_from_png_sequence(recorder.sequence_dir, recorder.fps, output_path);
    std::filesystem::remove_all(recorder.sequence_dir);
    recorder.status =
        "saved " + output_path.string() + " (" + std::to_string(recorder.frame_count) + " frames)";
    recorder.pending_stop = false;
    recorder.recording = false;
}

bool telemetry_event_visible(const animus::telemetry_core::Event &event,
                             const TelemetryEventFilters &filters)
{
    switch (event.severity)
    {
    case animus::telemetry_core::EventSeverity::Info:
        return filters.show_info;
    case animus::telemetry_core::EventSeverity::Warning:
        return filters.show_warnings;
    case animus::telemetry_core::EventSeverity::Error:
        return filters.show_errors;
    }
    return true;
}

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
                        ScreenshotToolState &screenshot_tool,
                        Mp4RecorderState &mp4_recorder,
                        UiState &ui_state,
                        bool &state_colors,
                        bool &highlight_fallback,
                        bool &overlay_enabled,
                        float &overlay_opacity)
{
    draw_top_status_bar(options, stats, snapshot, playback, mp4_recorder);
    draw_nav(ui_state);

    const float inspector_reserve = ImGui::GetIO().DisplaySize.x >= 980.0F ? 332.0F : 24.0F;
    const float main_width =
        std::clamp(ImGui::GetIO().DisplaySize.x - 144.0F - inspector_reserve, 300.0F, 460.0F);
    ImGui::SetNextWindowPos(ImVec2(144.0F, 46.0F), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(main_width, 390.0F), ImGuiCond_Always);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin(mode_label(ui_state.active_mode), nullptr, flags);
    switch (ui_state.active_mode)
    {
    case UiNavigationMode::View:
        ui_state.inspector_target = InspectorTarget::Terrain;
        draw_view_panel(
            options, pack_root, camera, selected_zoom, state_colors, highlight_fallback);
        break;
    case UiNavigationMode::Layers:
        ui_state.inspector_target = InspectorTarget::Layer;
        draw_layer_panel(options, overlay_enabled, overlay_opacity);
        break;
    case UiNavigationMode::Telemetry:
        ui_state.inspector_target =
            playback.loaded ? InspectorTarget::Entity : InspectorTarget::TelemetrySource;
        draw_telemetry_panel(options, playback, ui_state);
        break;
    case UiNavigationMode::Capture:
        ui_state.inspector_target = InspectorTarget::None;
        draw_capture_panel(screenshot_tool, mp4_recorder);
        break;
    case UiNavigationMode::Developer:
        ui_state.inspector_target = InspectorTarget::TelemetrySource;
        draw_developer_panel(options,
                             gl_info,
                             stats,
                             snapshot,
                             visible_tiles,
                             upload_bytes_used,
                             texture_uploads_used,
                             mesh_uploads_used,
                             resident_gpu_bytes,
                             playback,
                             ui_state);
        break;
    }
    ImGui::End();

    draw_inspector(playback, ui_state);
    draw_bottom_timeline(playback);
}

} // namespace animus::app
