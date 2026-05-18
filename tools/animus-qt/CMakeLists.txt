cmake_minimum_required(VERSION 3.21)

project(animus_qt LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(SQLite3 REQUIRED)

find_package(
    Qt6 6.4 REQUIRED
    COMPONENTS Core
               Gui
               Qml
               Quick
               QuickControls2
               Positioning
               Network
               WebChannel
               WebEngineQuick
               Test
)

qt_add_executable(
    animus_qt
    src/main.cpp
    src/models/VehicleModel.cpp
    src/telemetry/BreadcrumbPathModel.cpp
    src/telemetry/MavlinkDecoder.cpp
    src/telemetry/TelemetryService.cpp
    src/maps/MapSourceRegistry.cpp
    src/maps/OfflineMapManager.cpp
    src/maps/qgc/AnimusMapCacheManager.cpp
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
            Qt6::WebChannel
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
    qml/SetupView.qml
    RESOURCES
    web/cesium/index.html
    web/cesium/animus-cesium.js
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
    src/telemetry/BreadcrumbPathModel.cpp
    src/telemetry/MavlinkDecoder.cpp
    src/telemetry/TelemetryService.cpp
    src/models/VehicleModel.cpp
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
