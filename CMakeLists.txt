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
option(ALTAIR_WARNINGS_AS_ERRORS "Treat compiler warnings as errors for Altair host targets" OFF)
option(ALTAIR_BUILD_ANIMUS_QT "Build the experimental Qt/QML Animus shell" OFF)

function(altair_enable_warnings target)
    if(NOT ALTAIR_ENABLE_WARNINGS)
        return()
    endif()

    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
        if(ALTAIR_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        if(ALTAIR_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

enable_testing()

add_subdirectory(bayek)
add_subdirectory(vehicle)
add_subdirectory(tests)

if(ALTAIR_BUILD_ANIMUS_QT)
    add_subdirectory(tools/animus-qt)
endif()
