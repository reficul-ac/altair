add_library(animus_vehicle_core src/vehicle_definition.cpp src/vehicle_model.cpp)

target_include_directories(
    animus_vehicle_core
    PUBLIC include
    PRIVATE ../../third_party/cgltf
)

animus_enable_warnings(animus_vehicle_core)
