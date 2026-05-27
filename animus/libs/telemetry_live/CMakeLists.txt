find_package(asio REQUIRED)

add_library(
    animus_telemetry_live
    src/live_telemetry_buffer.cpp
    src/trail_decimation.cpp
    src/udp_mavlink_receiver.cpp
)

target_include_directories(animus_telemetry_live PUBLIC include)

target_link_libraries(animus_telemetry_live PUBLIC animus_telemetry_core asio::asio)

animus_enable_warnings(animus_telemetry_live)
