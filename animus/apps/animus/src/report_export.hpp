#pragma once

#include "app_config.hpp"
#include "ghost_replay.hpp"
#include "timeline_review.hpp"

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace animus::app
{

struct ReportExportInput
{
    std::string run_source;
    std::string config_profile;
    TelemetryRunSummary run;
    TimelineReviewData review;
    std::vector<TimelineReviewMarker> events;
    SelectedVehicleTestMetadata selected_vehicle_test;
    std::optional<GhostReplayComparison> ghost;
};

struct ReportExportResult
{
    bool ok = false;
    std::filesystem::path directory;
    std::vector<std::filesystem::path> files;
    std::vector<std::string> diagnostics;
};

struct ReportExportUiState
{
    std::array<char, 512> output_dir{};
    bool include_summary_yaml = true;
    bool include_summary_markdown = true;
    bool include_events_csv = true;
    bool include_plot_csvs = true;
    bool include_screenshots_directory = true;
    ReportExportResult last_result;
    std::vector<std::string> diagnostics;
};

[[nodiscard]] ReportExportResult export_report_v1(const ReportExportInput &input,
                                                  const std::filesystem::path &directory);
[[nodiscard]] ReportExportResult export_report_v1_from_ui(const ReportExportInput &input,
                                                          const ReportExportUiState &state,
                                                          const std::filesystem::path &fallback);
[[nodiscard]] std::filesystem::path report_export_output_dir(const ReportExportUiState &state,
                                                             const std::filesystem::path &fallback);
[[nodiscard]] std::vector<std::string>
report_export_preview_lines(const ReportExportInput &input, const ReportExportUiState &state);

} // namespace animus::app
