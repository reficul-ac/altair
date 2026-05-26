find_package(PNG REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(CURL REQUIRED)
find_package(JPEG REQUIRED)
find_package(PROJ CONFIG)
if(ANIMUS_ENABLE_GDAL)
    find_package(GDAL REQUIRED)
endif()

add_library(
    animus_terrain_core
    src/cache_metadata.cpp
    src/contracts.cpp
    src/datum.cpp
    src/terrain_cache.cpp
    src/terrain_data.cpp
    src/terrain_stream.cpp
    src/tile_source.cpp
)

target_include_directories(animus_terrain_core PUBLIC include)

target_link_libraries(
    animus_terrain_core
    PUBLIC animus_geo_core
    PRIVATE PNG::PNG SQLite::SQLite3 CURL::libcurl JPEG::JPEG
)

if(ANIMUS_ENABLE_GDAL)
    target_link_libraries(animus_terrain_core PRIVATE GDAL::GDAL)
    target_compile_definitions(animus_terrain_core PRIVATE ANIMUS_TERRAIN_CORE_HAS_GDAL=1)
endif()

if(PROJ_FOUND)
    target_link_libraries(animus_terrain_core PRIVATE PROJ::proj)
    target_compile_definitions(animus_terrain_core PRIVATE ANIMUS_TERRAIN_CORE_HAS_PROJ=1)
    find_path(
        ANIMUS_PROJ_INCLUDE_DIR proj.h
        HINTS ${proj_PACKAGE_FOLDER_DEBUG}/include ${proj_PACKAGE_FOLDER_RELEASE}/include
              ${proj_PACKAGE_FOLDER_RELWITHDEBINFO}/include
              ${proj_PACKAGE_FOLDER_MINSIZEREL}/include
    )
    if(ANIMUS_PROJ_INCLUDE_DIR)
        target_include_directories(animus_terrain_core PRIVATE ${ANIMUS_PROJ_INCLUDE_DIR})
    endif()
endif()

animus_enable_warnings(animus_terrain_core)
