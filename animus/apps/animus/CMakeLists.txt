find_package(PNG REQUIRED)
find_package(yaml-cpp REQUIRED)

add_executable(
    animus
    src/app_config.cpp
    src/main.cpp
    src/animus_app.cpp
    src/capture.cpp
    src/fpv_camera.cpp
    src/forward_clearance.cpp
    src/ghost_replay.cpp
    src/layer_offline.cpp
    src/layer_stack_model.cpp
    src/mavlink_inspector.cpp
    src/options.cpp
    src/plot_config.cpp
    src/plot_series_buffer.cpp
    src/plot_ui.cpp
    src/plan_visualization.cpp
    src/selected_vehicle_card.cpp
    src/report_export.cpp
    src/telemetry_signal_catalog.cpp
    src/status_ribbon.cpp
    src/timeline_review.cpp
    src/timeline_transport.cpp
    src/ui.cpp
    src/ui_theme.cpp
    src/vehicle_visual_style.cpp
    src/workspace_layout_model.cpp
)

target_link_libraries(
    animus
    PRIVATE animus_geo_core
            animus_render_core
            animus_terrain_core
            animus_telemetry_core
            animus_telemetry_live
            animus_vehicle_core
            PNG::PNG
            yaml-cpp::yaml-cpp
)

target_compile_definitions(animus PRIVATE ANIMUS_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

animus_enable_warnings(animus)

if(BUILD_TESTING AND ANIMUS_ENABLE_OPENGL_TESTS)
    add_test(NAME animus_app_opengl_smoke COMMAND animus --smoke --frames 3)
endif()
