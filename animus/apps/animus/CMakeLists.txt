find_package(PNG REQUIRED)

add_executable(
    animus
    src/main.cpp
    src/animus_app.cpp
    src/capture.cpp
    src/options.cpp
    src/ui.cpp
)

target_link_libraries(
    animus
    PRIVATE animus_geo_core
            animus_render_core
            animus_terrain_core
            animus_telemetry_core
            animus_telemetry_live
            PNG::PNG
)

target_compile_definitions(animus PRIVATE ANIMUS_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

animus_enable_warnings(animus)

if(BUILD_TESTING AND ANIMUS_ENABLE_OPENGL_TESTS)
    add_test(NAME animus_app_opengl_smoke COMMAND animus --smoke --frames 3)
endif()
