add_library(animus_telemetry_core src/mavlink.cpp src/playback.cpp src/tlog.cpp)

target_include_directories(animus_telemetry_core PUBLIC include)

animus_enable_warnings(animus_telemetry_core)
