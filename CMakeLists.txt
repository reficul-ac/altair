cmake_minimum_required(VERSION 3.16)

project(altair C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE
        Debug
        CACHE STRING "Build type" FORCE
    )
endif()

option(ALTAIR_ENABLE_WARNINGS "Enable compiler warnings for Altair host targets" ON)

function(altair_enable_warnings target)
    if(NOT ALTAIR_ENABLE_WARNINGS)
        return()
    endif()

    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

enable_testing()

add_subdirectory(bayek)
add_subdirectory(vehicle)
add_subdirectory(tests)
