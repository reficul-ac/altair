#include "animus/vehicle_core/vehicle_definition.hpp"
#include "animus/vehicle_core/vehicle_model.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace
{

std::filesystem::path source_vehicle_root()
{
    return std::filesystem::path(ANIMUS_SOURCE_DIR) / "assets" / "vehicles";
}

std::filesystem::path unique_temp_root(const std::string &name)
{
    const auto root = std::filesystem::temp_directory_path() /
                      ("animus_vehicle_core_tests_" + name + "_" +
                       std::to_string(::testing::UnitTest::GetInstance()->random_seed()));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    return root;
}

void write_text(const std::filesystem::path &path, const std::string &text)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path);
    ASSERT_TRUE(output);
    output << text;
}

std::string valid_descriptor(const std::string &id,
                             const std::string &type = "rc_plane",
                             const std::string &model = "model.glb")
{
    return "id: " + id +
           "\n"
           "display_name: Test Vehicle\n"
           "type: " +
           type +
           "\n"
           "model:\n"
           "  path: " +
           model +
           "\n"
           "  scale: 0.05\n"
           "orientation:\n"
           "  yaw_deg: 0\n"
           "  pitch_deg: 0\n"
           "  roll_deg: 0\n"
           "dimensions:\n"
           "  length_m: 1.0\n"
           "  wingspan_m: 1.4\n"
           "  height_m: 0.2\n";
}

bool has_diagnostic_containing(const animus::vehicle_core::VehicleRegistry &registry,
                               const std::string &needle)
{
    for (const auto &diagnostic : registry.diagnostics())
    {
        if (diagnostic.message.find(needle) != std::string::npos)
        {
            return true;
        }
    }
    return false;
}

} // namespace

TEST(VehicleCore, BuiltInGenericRcPlaneDescriptorLoads)
{
    const auto registry = animus::vehicle_core::VehicleRegistry::load_from_directory(
        source_vehicle_root());
    const auto *definition = registry.default_definition();
    ASSERT_NE(definition, nullptr);
    EXPECT_EQ(definition->id, "animus.rc_plane.generic");
    EXPECT_EQ(definition->display_name, "Generic RC Plane");
    EXPECT_EQ(definition->type, animus::vehicle_core::VehicleType::RcPlane);
    EXPECT_EQ(definition->model_path.extension(), ".glb");
    EXPECT_TRUE(std::filesystem::exists(definition->model_path));
}

TEST(VehicleCore, RequiredFieldFailuresAreReported)
{
    const auto root = unique_temp_root("missing_required");
    write_text(root / "pkg" / "vehicle.animus.yaml",
               "id: animus.test.missing\n"
               "type: rc_plane\n");

    const auto registry = animus::vehicle_core::VehicleRegistry::load_from_directory(root);
    EXPECT_TRUE(registry.definitions().empty());
    EXPECT_TRUE(has_diagnostic_containing(registry, "missing required field"));
}

TEST(VehicleCore, UnknownTypeIsRejected)
{
    const auto root = unique_temp_root("unknown_type");
    write_text(root / "pkg" / "vehicle.animus.yaml",
               valid_descriptor("animus.test.unknown", "quadrotor"));

    const auto registry = animus::vehicle_core::VehicleRegistry::load_from_directory(root);
    EXPECT_TRUE(registry.definitions().empty());
    EXPECT_TRUE(has_diagnostic_containing(registry, "unknown vehicle type"));
}

TEST(VehicleCore, MissingModelReportsWarningButKeepsDefinition)
{
    const auto root = unique_temp_root("missing_model");
    write_text(root / "pkg" / "vehicle.animus.yaml", valid_descriptor("animus.test.missing_model"));

    const auto registry = animus::vehicle_core::VehicleRegistry::load_from_directory(root);
    ASSERT_EQ(registry.definitions().size(), 1U);
    EXPECT_TRUE(has_diagnostic_containing(registry, "model file is missing"));
}

TEST(VehicleCore, DuplicateIdsAreRejected)
{
    const auto root = unique_temp_root("duplicate");
    write_text(root / "a" / "vehicle.animus.yaml", valid_descriptor("animus.test.duplicate"));
    write_text(root / "a" / "model.glb", "");
    write_text(root / "b" / "vehicle.animus.yaml", valid_descriptor("animus.test.duplicate"));
    write_text(root / "b" / "model.glb", "");

    const auto registry = animus::vehicle_core::VehicleRegistry::load_from_directory(root);
    EXPECT_EQ(registry.definitions().size(), 1U);
    EXPECT_TRUE(has_diagnostic_containing(registry, "duplicate vehicle id"));
}

TEST(VehicleCore, GeneratedRcPlaneGlbParses)
{
    const auto registry = animus::vehicle_core::VehicleRegistry::load_from_directory(
        source_vehicle_root());
    const auto *definition = registry.default_definition();
    ASSERT_NE(definition, nullptr);

    const auto model = animus::vehicle_core::load_glb_model(definition->model_path);
    ASSERT_FALSE(model.primitives.empty());
    std::size_t vertices = 0U;
    std::size_t indices = 0U;
    for (const auto &primitive : model.primitives)
    {
        vertices += primitive.vertices.size();
        indices += primitive.indices.size();
    }
    EXPECT_GT(vertices, 0U);
    EXPECT_GT(indices, 0U);
}

TEST(VehicleCore, CorruptGlbReturnsReadableError)
{
    const auto root = unique_temp_root("corrupt_glb");
    const auto path = root / "corrupt.glb";
    write_text(path, "not a glb");

    try
    {
        (void)animus::vehicle_core::load_glb_model(path);
        FAIL() << "expected corrupt GLB load to throw";
    }
    catch (const std::exception &error)
    {
        EXPECT_NE(std::string(error.what()).find("GLB"), std::string::npos);
    }
}
