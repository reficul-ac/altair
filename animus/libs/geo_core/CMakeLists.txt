add_library(animus_geo_core src/tile_math.cpp)

target_include_directories(animus_geo_core PUBLIC include)

animus_enable_warnings(animus_geo_core)
