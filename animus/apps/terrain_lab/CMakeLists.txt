find_package(PNG REQUIRED)

add_library(terrain_lab_core
    src/terrain_data.cpp
)

target_include_directories(terrain_lab_core
    PUBLIC
        include
)

target_link_libraries(terrain_lab_core
    PUBLIC
        animus_render_core
    PRIVATE
        PNG::PNG
)

animus_enable_warnings(terrain_lab_core)

add_executable(terrain_lab
    src/main.cpp
)

target_link_libraries(terrain_lab
    PRIVATE
        terrain_lab_core
)

target_compile_definitions(terrain_lab
    PRIVATE
        ANIMUS_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

animus_enable_warnings(terrain_lab)

if(BUILD_TESTING AND ANIMUS_ENABLE_OPENGL_TESTS)
    add_test(
        NAME animus_terrain_lab_opengl_smoke
        COMMAND terrain_lab --smoke --frames 3
    )
endif()

if(BUILD_TESTING)
    add_executable(terrain_lab_tests
        tests/terrain_data_tests.cpp
    )

    target_link_libraries(terrain_lab_tests
        PRIVATE
            terrain_lab_core
            GTest::gtest_main
    )

    animus_enable_warnings(terrain_lab_tests)

    include(GoogleTest)
    gtest_discover_tests(terrain_lab_tests)
endif()
