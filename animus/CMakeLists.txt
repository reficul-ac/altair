cmake_minimum_required(VERSION 3.20)

project(animus LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE
        Debug
        CACHE STRING "Build type" FORCE
    )
endif()

option(ANIMUS_ENABLE_WARNINGS "Enable compiler warnings for Animus targets" ON)
option(ANIMUS_WARNINGS_AS_ERRORS "Treat compiler warnings as errors for Animus targets" OFF)
option(ANIMUS_ENABLE_OPENGL_TESTS "Enable OpenGL smoke tests that require a graphics context" OFF)

function(animus_enable_warnings target)
    if(NOT ANIMUS_ENABLE_WARNINGS)
        return()
    endif()

    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
        if(ANIMUS_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        if(ANIMUS_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

include(CTest)

if(BUILD_TESTING)
    find_package(GTest REQUIRED)
endif()

add_subdirectory(libs)
add_subdirectory(apps)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
