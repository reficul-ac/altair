add_executable(
    animus_contract_tests contracts/contract_tests.cpp terrain_core_tests.cpp
                          telemetry_core_tests.cpp
)

target_link_libraries(
    animus_contract_tests PRIVATE animus_data_core animus_terrain_core animus_telemetry_core
                                  GTest::gtest_main
)

if(ANIMUS_ENABLE_GDAL)
    find_package(GDAL REQUIRED)
    target_link_libraries(animus_contract_tests PRIVATE GDAL::GDAL)
    target_compile_definitions(animus_contract_tests PRIVATE ANIMUS_TESTS_HAVE_GDAL=1)
endif()

animus_enable_warnings(animus_contract_tests)

target_compile_definitions(animus_contract_tests PRIVATE ANIMUS_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

include(GoogleTest)
gtest_discover_tests(animus_contract_tests)

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
