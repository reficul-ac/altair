add_library(
    animus_telemetry_core
    src/import_common.cpp
    src/mavlink.cpp
    src/mavlink_reducer.cpp
    src/playback.cpp
    src/timeline_builder.cpp
    src/tlog.cpp
)

target_include_directories(animus_telemetry_core PUBLIC include)

if(ANIMUS_ENABLE_TELEMETRY_IMPORTS)
    find_package(protobuf REQUIRED)
    find_package(HighFive CONFIG REQUIRED)

    include(FetchContent)
    FetchContent_Declare(
        mcap
        GIT_REPOSITORY https://github.com/foxglove/mcap.git
        GIT_TAG releases/cpp/v2.1.3
    )
    FetchContent_MakeAvailable(mcap)
    add_library(animus_mcap_headers INTERFACE)
    target_include_directories(animus_mcap_headers INTERFACE ${mcap_SOURCE_DIR}/cpp/mcap/include)
    target_compile_definitions(
        animus_mcap_headers INTERFACE MCAP_COMPRESSION_NO_LZ4 MCAP_COMPRESSION_NO_ZSTD
    )

    set(ANIMUS_TELEMETRY_PROTO ${CMAKE_SOURCE_DIR}/schemas/telemetry/animus_telemetry_v1.proto)
    add_library(animus_telemetry_proto)
    protobuf_generate(
        TARGET
        animus_telemetry_proto
        LANGUAGE
        cpp
        PROTOS
        ${ANIMUS_TELEMETRY_PROTO}
        IMPORT_DIRS
        ${CMAKE_SOURCE_DIR}/schemas/telemetry
        PROTOC_OUT_DIR
        ${CMAKE_CURRENT_BINARY_DIR}
    )
    target_include_directories(animus_telemetry_proto SYSTEM PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
    target_link_libraries(animus_telemetry_proto PUBLIC protobuf::protobuf)

    target_sources(
        animus_telemetry_core PRIVATE src/canonical_proto.cpp src/hdf5_import.cpp
                                      src/mcap_import.cpp
    )
    target_link_libraries(animus_telemetry_core PRIVATE animus_telemetry_proto animus_mcap_headers)
    target_link_libraries(animus_telemetry_core PRIVATE HighFive::HighFive)
    target_compile_definitions(animus_telemetry_core PUBLIC ANIMUS_ENABLE_TELEMETRY_IMPORTS=1)
else()
    target_sources(animus_telemetry_core PRIVATE src/disabled_imports.cpp)
endif()

animus_enable_warnings(animus_telemetry_core)
