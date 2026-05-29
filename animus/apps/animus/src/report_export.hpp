#pragma once

#include "app_config.hpp"
#include "ghost_replay.hpp"
#include "timeline_review.hpp"

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

[[nodiscard]] ReportExportResult export_report_v1(const ReportExportInput &input,
                                                  const std::filesystem::path &directory);

} // namespace animus::app
