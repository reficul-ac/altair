cmake_minimum_required(VERSION 3.16)

project(altair C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

enable_testing()

add_subdirectory(framework/common)
add_subdirectory(vehicles/altair)
add_subdirectory(framework/fsw)
add_subdirectory(framework/telemetry)
add_subdirectory(framework/sim)
add_subdirectory(framework/mc)
add_subdirectory(tests)
