add_executable(
    animus_contract_tests contracts/contract_tests.cpp terrain_core_tests.cpp
                          telemetry_core_tests.cpp telemetry_live_tests.cpp vehicle_core_tests.cpp
)

target_link_libraries(
    animus_contract_tests
    PRIVATE animus_data_core
            animus_terrain_core
            animus_telemetry_core
            animus_telemetry_live
            animus_vehicle_core
            GTest::gtest_main
)
if(ANIMUS_ENABLE_TELEMETRY_IMPORTS)
    find_package(HighFive CONFIG REQUIRED)
    target_link_libraries(
        animus_contract_tests PRIVATE animus_telemetry_proto animus_mcap_headers HighFive::HighFive
    )
    target_compile_definitions(animus_contract_tests PRIVATE ANIMUS_TESTS_HAVE_IMPORTS=1)
endif()

if(ANIMUS_ENABLE_GDAL)
    find_package(GDAL REQUIRED)
    target_link_libraries(animus_contract_tests PRIVATE GDAL::GDAL)
    target_compile_definitions(animus_contract_tests PRIVATE ANIMUS_TESTS_HAVE_GDAL=1)
endif()

animus_enable_warnings(animus_contract_tests)

target_compile_definitions(animus_contract_tests PRIVATE ANIMUS_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

include(GoogleTest)
gtest_discover_tests(animus_contract_tests)

add_executable(animus_options_tests ../apps/animus/src/options.cpp options_tests.cpp)
target_include_directories(animus_options_tests PRIVATE ../apps/animus/src)
target_link_libraries(animus_options_tests PRIVATE animus_telemetry_core GTest::gtest_main)
animus_enable_warnings(animus_options_tests)
gtest_discover_tests(animus_options_tests)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_test(NAME animus_python_tool_tests COMMAND ${Python3_EXECUTABLE} -m unittest discover -s
                                               ${CMAKE_CURRENT_SOURCE_DIR}/tools
)

add_test(
    NAME animus_lake_tahoe_downloader_dry_run
    COMMAND
        ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/download_lake_tahoe_pack.py --manifest
        ${CMAKE_SOURCE_DIR}/data/sample_areas/lake_tahoe_phase_d.json --pack-root
        ${CMAKE_SOURCE_DIR}/data/tiles/lake_tahoe --dry-run
)

add_test(
    NAME animus_lake_tahoe_phase_h_downloader_dry_run
    COMMAND
        ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/download_lake_tahoe_pack.py --manifest
        ${CMAKE_SOURCE_DIR}/data/sample_areas/lake_tahoe_phase_h.json --pack-root
        ${CMAKE_SOURCE_DIR}/data/tiles/lake_tahoe --dry-run
)
