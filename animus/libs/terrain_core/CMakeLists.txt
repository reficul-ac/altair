add_library(animus_terrain_core
    src/contracts.cpp
)

target_include_directories(animus_terrain_core
    PUBLIC
        include
)

animus_enable_warnings(animus_terrain_core)
