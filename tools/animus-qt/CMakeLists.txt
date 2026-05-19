cmake_minimum_required(VERSION 3.21)

project(animus_qt LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(SQLite3 REQUIRED)

file(
    GLOB_RECURSE ANIMUS_CESIUM_VENDOR_RESOURCES
    RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    CONFIGURE_DEPENDS "web/cesium/vendor/Cesium/*"
)
file(
    GLOB_RECURSE ANIMUS_CESIUM_FIXTURE_RESOURCES
    RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    CONFIGURE_DEPENDS "web/cesium/fixture/*"
)
file(
    GLOB_RECURSE ANIMUS_CESIUM_MODEL_RESOURCES
    RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    CONFIGURE_DEPENDS "web/cesium/models/*"
)

find_package(
    Qt6 6.4 REQUIRED
    COMPONENTS Core
               Gui
               Qml
               Quick
               QuickControls2
               Positioning
               Network
               WebEngineQuick
               Test
)

qt_add_executable(
    animus_qt
    src/main.cpp
    src/models/VehicleModel.cpp
    src/models/VehicleModelProfileManager.cpp
    src/telemetry/BreadcrumbPathModel.cpp
    src/telemetry/MavlinkDecoder.cpp
    src/telemetry/TelemetryService.cpp
    src/maps/MapSourceRegistry.cpp
    src/maps/OfflineMapManager.cpp
    src/maps/qgc/AnimusMapCacheManager.cpp
    src/maps/TerrainClearanceAnalyzer.cpp
    src/maps/CesiumBridge.cpp
)

target_include_directories(animus_qt PRIVATE src)
target_compile_features(animus_qt PRIVATE cxx_std_17)
target_compile_definitions(animus_qt PRIVATE ANIMUS_REPO_ROOT="${CMAKE_SOURCE_DIR}")

target_link_libraries(
    animus_qt
    PRIVATE Qt6::Core
            Qt6::Gui
            Qt6::Qml
            Qt6::Quick
            Qt6::QuickControls2
            Qt6::Positioning
            Qt6::Network
            Qt6::WebEngineQuick
            SQLite::SQLite3
)

qt_add_qml_module(
    animus_qt
    URI
    Animus
    VERSION
    1.0
    QML_FILES
    qml/Main.qml
    qml/WorkspaceShell.qml
    qml/Map2DView.qml
    qml/TelemetryStrip.qml
    qml/Terrain3DView.qml
    qml/Terrain3DWebView.qml
    qml/FpvView.qml
    qml/TacticalAttitudeView.qml
    qml/SetupView.qml
    RESOURCES
    web/cesium/index.html
    web/cesium/animus-cesium.js
    web/cesium/vehicleModel.js
    web/cesium/vendor/README.md
    web/cesium/vendor/LICENSES.md
    ${ANIMUS_CESIUM_FIXTURE_RESOURCES}
    ${ANIMUS_CESIUM_MODEL_RESOURCES}
    ${ANIMUS_CESIUM_VENDOR_RESOURCES}
)

if(COMMAND altair_enable_warnings)
    altair_enable_warnings(animus_qt)
endif()

add_executable(
    animus_qt_unit_tests
    tests/test_map_models.cpp
    src/maps/MapSourceRegistry.cpp
    src/maps/OfflineMapManager.cpp
    src/maps/qgc/AnimusMapCacheManager.cpp
    src/maps/TerrainClearanceAnalyzer.cpp
    src/maps/CesiumBridge.cpp
    src/telemetry/BreadcrumbPathModel.cpp
    src/telemetry/MavlinkDecoder.cpp
    src/telemetry/TelemetryService.cpp
    src/models/VehicleModel.cpp
    src/models/VehicleModelProfileManager.cpp
)
target_include_directories(animus_qt_unit_tests PRIVATE src)
target_compile_features(animus_qt_unit_tests PRIVATE cxx_std_17)
target_compile_definitions(
    animus_qt_unit_tests PRIVATE ANIMUS_QT_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml"
                                 ANIMUS_REPO_ROOT="${CMAKE_SOURCE_DIR}"
)
target_link_libraries(
    animus_qt_unit_tests
    PRIVATE Qt6::Core
            Qt6::Gui
            Qt6::Network
            Qt6::Positioning
            Qt6::Qml
            Qt6::Quick
            Qt6::QuickControls2
            Qt6::Test
            SQLite::SQLite3
)
if(COMMAND altair_enable_warnings)
    altair_enable_warnings(animus_qt_unit_tests)
endif()
add_test(NAME animus_qt_map_models COMMAND animus_qt_unit_tests)
set_tests_properties(animus_qt_map_models PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
