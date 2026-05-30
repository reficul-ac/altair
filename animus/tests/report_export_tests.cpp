#include "report_export.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>

#include <gtest/gtest.h>

TEST(ReportExport, WritesStaticV1Artifacts)
{
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "animus_report_export_tests";
    std::filesystem::remove_all(dir);
    animus::app::ReportExportInput input;
    input.run_source = "run.tlog";
    input.config_profile = "default";
    input.run.available = true;
    input.run.duration_s = 12.0;
    input.run.distance_m = 34.0;
    input.selected_vehicle_test.test_name = "FT-12";
    input.review.altitude.unit = "m";
    input.review.altitude.points.push_back({0.0, 100.0});
    animus::app::TimelineReviewMarker event;
    event.time_s = 1.0;
    event.category = animus::app::TimelineReviewMarkerCategory::Capture;
    event.label = "screenshot";
    input.events.push_back(event);

    const auto result = animus::app::export_report_v1(input, dir);

    EXPECT_TRUE(result.ok);
    EXPECT_TRUE(std::filesystem::exists(dir / "summary.yaml"));
    EXPECT_TRUE(std::filesystem::exists(dir / "summary.md"));
    EXPECT_TRUE(std::filesystem::exists(dir / "events.csv"));
    EXPECT_TRUE(std::filesystem::exists(dir / "plots" / "altitude.csv"));
    EXPECT_TRUE(std::filesystem::exists(dir / "screenshots"));
    std::filesystem::remove_all(dir);
}

TEST(ReportExport, UiStateBuildsPreviewAndUsesFallbackDirectory)
{
    animus::app::ReportExportInput input;
    input.run_source = "run.tlog";
    input.run.duration_s = 3.5;
    animus::app::TimelineReviewMarker event;
    event.time_s = 1.0;
    input.events.push_back(event);
    animus::app::ReportExportUiState state;
    state.include_plot_csvs = false;
    const std::filesystem::path fallback = "artifacts/animus/reports";

    const auto output = animus::app::report_export_output_dir(state, fallback);
    const auto preview = animus::app::report_export_preview_lines(input, state);

    EXPECT_EQ(output, fallback);
    EXPECT_NE(std::find(preview.begin(), preview.end(), "summary.yaml"), preview.end());
    EXPECT_EQ(std::find(preview.begin(), preview.end(), "plots/*.csv"), preview.end());
}

TEST(ReportExport, UiExportAppliesArtifactToggles)
{
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "animus_report_export_ui_tests";
    std::filesystem::remove_all(dir);
    animus::app::ReportExportInput input;
    input.run_source = "run.tlog";
    input.review.altitude.unit = "m";
    input.review.altitude.points.push_back({0.0, 100.0});
    animus::app::ReportExportUiState state;
    std::snprintf(state.output_dir.data(), state.output_dir.size(), "%s", dir.string().c_str());
    state.include_summary_markdown = false;
    state.include_plot_csvs = false;

    const auto result = animus::app::export_report_v1_from_ui(input, state, {});

    EXPECT_TRUE(result.ok);
    EXPECT_TRUE(std::filesystem::exists(dir / "summary.yaml"));
    EXPECT_FALSE(std::filesystem::exists(dir / "summary.md"));
    EXPECT_FALSE(std::filesystem::exists(dir / "plots"));
    std::filesystem::remove_all(dir);
}
