#include "report_export.hpp"

#include <fstream>
#include <system_error>

namespace animus::app
{
namespace
{

std::string value_or_na(const std::optional<double> value)
{
    return value ? std::to_string(*value) : std::string("n/a");
}

void write_series_csv(const std::filesystem::path &path, const TimelineReviewSeries &series)
{
    std::ofstream out(path);
    out << "time_s,value_" << series.unit << "\n";
    for (const TimelineReviewPoint &point : series.points)
    {
        out << point.time_s << "," << point.value << "\n";
    }
}

} // namespace

ReportExportResult export_report_v1(const ReportExportInput &input,
                                    const std::filesystem::path &directory)
{
    ReportExportResult result;
    result.directory = directory;
    if (directory.empty())
    {
        result.diagnostics.push_back("report export directory is empty");
        return result;
    }
    std::error_code error;
    std::filesystem::create_directories(directory / "plots", error);
    if (error)
    {
        result.diagnostics.push_back("failed to create report directory: " + error.message());
        return result;
    }
    std::filesystem::create_directories(directory / "screenshots", error);
    if (error)
    {
        result.diagnostics.push_back("failed to create screenshots directory: " + error.message());
        return result;
    }

    const std::filesystem::path yaml_path = directory / "summary.yaml";
    {
        std::ofstream out(yaml_path);
        out << "version: 1\n";
        out << "run_source: " << input.run_source << "\n";
        out << "config_profile: " << input.config_profile << "\n";
        out << "duration_s: " << input.run.duration_s << "\n";
        out << "distance_m: " << input.run.distance_m << "\n";
        out << "max_speed_mps: " << value_or_na(input.run.max_speed_mps) << "\n";
        out << "max_roll_deg: " << value_or_na(input.run.max_roll_deg) << "\n";
        out << "max_pitch_deg: " << value_or_na(input.run.max_pitch_deg) << "\n";
        out << "telemetry_gap_count: " << input.run.telemetry_gap_count << "\n";
        out << "route_completion_ratio: " << value_or_na(input.run.route_completion_ratio) << "\n";
        out << "max_plan_deviation_m: " << value_or_na(input.run.max_plan_deviation_m) << "\n";
        out << "event_count: " << input.events.size() << "\n";
        out << "selected_vehicle:\n";
        out << "  test_name: " << input.selected_vehicle_test.test_name << "\n";
        out << "  phase: " << input.selected_vehicle_test.phase << "\n";
        out << "  target_speed: " << input.selected_vehicle_test.target_speed << "\n";
        out << "  target_altitude: " << input.selected_vehicle_test.target_altitude << "\n";
        out << "  target_heading: " << input.selected_vehicle_test.target_heading << "\n";
        if (input.ghost)
        {
            out << "ghost_replay:\n";
            out << "  baseline_loaded: " << (input.ghost->baseline_loaded ? "true" : "false")
                << "\n";
            out << "  duration_delta_s: " << input.ghost->duration_delta_s << "\n";
            out << "  distance_delta_m: " << input.ghost->distance_delta_m << "\n";
        }
    }
    result.files.push_back(yaml_path);

    const std::filesystem::path markdown_path = directory / "summary.md";
    {
        std::ofstream out(markdown_path);
        out << "# Animus Report\n\n";
        out << "- Run source: " << input.run_source << "\n";
        out << "- Duration: " << input.run.duration_s << " s\n";
        out << "- Distance: " << input.run.distance_m << " m\n";
        out << "- Events: " << input.events.size() << "\n";
        out << "- Test: "
            << (input.selected_vehicle_test.test_name.empty() ? "n/a"
                                                               : input.selected_vehicle_test.test_name)
            << "\n";
    }
    result.files.push_back(markdown_path);

    const std::filesystem::path events_path = directory / "events.csv";
    {
        std::ofstream out(events_path);
        out << "time_s,end_time_s,severity,category,label,value\n";
        for (const TimelineReviewMarker &event : input.events)
        {
            out << event.time_s << "," << (event.end_time_s ? std::to_string(*event.end_time_s) : "")
                << "," << timeline_review_severity_label(event.severity) << ","
                << timeline_review_marker_label(event.category) << "," << event.label << ","
                << (event.value ? std::to_string(*event.value) : "") << "\n";
        }
    }
    result.files.push_back(events_path);

    write_series_csv(directory / "plots" / "altitude.csv", input.review.altitude);
    write_series_csv(directory / "plots" / "ground_speed.csv", input.review.ground_speed);
    write_series_csv(directory / "plots" / "terrain_clearance.csv", input.review.terrain_clearance);
    result.files.push_back(directory / "plots" / "altitude.csv");
    result.files.push_back(directory / "plots" / "ground_speed.csv");
    result.files.push_back(directory / "plots" / "terrain_clearance.csv");
    result.ok = true;
    return result;
}

} // namespace animus::app
