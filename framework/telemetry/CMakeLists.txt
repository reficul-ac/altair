add_library(altair_telemetry
  telemetry_packet.c
)

target_include_directories(altair_telemetry
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(altair_telemetry PUBLIC altair_common)
