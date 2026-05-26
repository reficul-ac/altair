find_package(PNG REQUIRED)
if(ANIMUS_ENABLE_GDAL)
    find_package(GDAL REQUIRED)
endif()

add_library(
    animus_terrain_core src/contracts.cpp src/terrain_cache.cpp src/terrain_data.cpp
                        src/terrain_stream.cpp
)

target_include_directories(animus_terrain_core PUBLIC include)

target_link_libraries(
    animus_terrain_core
    PUBLIC animus_geo_core
    PRIVATE PNG::PNG
)

if(ANIMUS_ENABLE_GDAL)
    target_link_libraries(animus_terrain_core PRIVATE GDAL::GDAL)
    target_compile_definitions(animus_terrain_core PRIVATE ANIMUS_TERRAIN_CORE_HAS_GDAL=1)
endif()

animus_enable_warnings(animus_terrain_core)
