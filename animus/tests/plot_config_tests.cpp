#include "plot_config.hpp"

#include <yaml-cpp/yaml.h>

#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace
{

bool has_diagnostic(const std::vector<std::string> &diagnostics, const std::string &needle)
{
    for (const std::string &diagnostic : diagnostics)
    {
        if (diagnostic.find(needle) != std::string::npos)
        {
            return true;
        }
    }
    return false;
}

animus::app::PlotShelfConfig round_trip(const animus::app::PlotShelfConfig &config,
                                        std::vector<std::string> &diagnostics)
{
    YAML::Emitter out;
    animus::app::write_plot_shelf_config(out, config);
    return animus::app::read_plot_shelf_config(YAML::Load(out.c_str()), diagnostics);
}

} // namespace

TEST(AnimusPlotConfig, DefaultsMatchPhaseThreePresets)
{
    const animus::app::PlotShelfConfig config = animus::app::default_plot_shelf_config();

    ASSERT_EQ(config.plots.size(), 5U);
    EXPECT_EQ(config.plots[0].title, "Altitude / Clearance");
    EXPECT_EQ(config.plots[0].series[0].signal.field_path, "altitude_msl_m");
    EXPECT_EQ(config.plots[0].series[1].signal.field_path, "altitude_relative_m");
    EXPECT_EQ(config.plots[0].series[2].signal.field_path, "terrain_clearance_m");
    EXPECT_EQ(config.plots[1].title, "Speed / Climb");
    EXPECT_EQ(config.plots[2].series[0].transform, animus::app::SignalTransform::RadToDeg);
    EXPECT_EQ(config.plots[3].series[0].signal.field_path, "link_hz");
    EXPECT_EQ(config.plots[4].series[0].signal.field_path, "frame_time_ms");
}

TEST(AnimusPlotConfig, YamlRoundTripPreservesDefinitions)
{
    animus::app::PlotShelfConfig config = animus::app::default_plot_shelf_config();
    config.visible = false;
    config.paused = true;
    config.follow_latest = false;
    config.default_time_window_s = 45.0;
    config.max_points_per_series = 300U;
    config.render_max_points = 75U;
    config.plots.resize(1U);
    config.plots.front().title = "Custom";
    config.plots.front().y_auto = false;
    config.plots.front().y_min = -10.0;
    config.plots.front().y_max = 10.0;
    config.plots.front().series.resize(1U);
    auto &series = config.plots.front().series.front();
    series.label = "VFR altitude";
    series.signal.source = animus::app::SignalSource::Mavlink;
    series.signal.field_path.clear();
    series.signal.mavlink_message = "GLOBAL_POSITION_INT";
    series.signal.mavlink_field = "relative_alt";
    series.transform = animus::app::SignalTransform::MetersToFeet;
    series.scale = 2.0;
    series.offset = 3.0;
    series.unit_override = "ft";
    series.entity_binding = animus::app::PlotEntityBindingMode::Explicit;
    series.explicit_entity = animus::telemetry_core::EntityId{7U, 1U};

    std::vector<std::string> diagnostics;
    const animus::app::PlotShelfConfig loaded = round_trip(config, diagnostics);

    EXPECT_TRUE(diagnostics.empty());
    EXPECT_EQ(loaded, config);
}

TEST(AnimusPlotConfig, InvalidDefinitionsFallBackToDefaults)
{
    const YAML::Node node = YAML::Load("visible: true\n"
                                       "plots:\n"
                                       "  - id: bad\n"
                                       "    title: Bad\n"
                                       "    series:\n"
                                       "      - id: s\n"
                                       "        label: broken\n"
                                       "        signal:\n"
                                       "          source: sample\n"
                                       "          field: not_real\n"
                                       "        transform: not_a_transform\n");
    std::vector<std::string> diagnostics;

    const animus::app::PlotShelfConfig loaded =
        animus::app::read_plot_shelf_config(node, diagnostics);

    EXPECT_EQ(loaded, animus::app::default_plot_shelf_config());
    EXPECT_TRUE(has_diagnostic(diagnostics, "transform"));
}

TEST(AnimusPlotConfig, MissingNodeReceivesDefaults)
{
    std::vector<std::string> diagnostics;

    const animus::app::PlotShelfConfig loaded =
        animus::app::read_plot_shelf_config(YAML::Node{}, diagnostics);

    EXPECT_TRUE(diagnostics.empty());
    EXPECT_EQ(loaded, animus::app::default_plot_shelf_config());
}
