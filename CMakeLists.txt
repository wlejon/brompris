cmake_minimum_required(VERSION 3.24)

project(brompris VERSION 0.1.0 LANGUAGES CXX)

# Dependencies (cmake/bro_deps.cmake): an outer project's target wins (under bro
# these are all bro's), else the ../<name> working tree beside the top-level
# project, else the pinned commit, fetched at configure.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/bro_deps.cmake)

if(MSVC)
    # The DLL CRT, so the tests share one heap with bronze_runtime_shared.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(_brompris_top_level OFF)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(_brompris_top_level ON)
endif()

option(BROMPRIS_BUILD_TESTS "Build the brompris tests" ${_brompris_top_level})

function(brompris_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /MP)
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

find_package(Threads REQUIRED)

set(BROMPRIS_SOURCES
    src/metadata.cpp
    src/player_model.cpp
    src/media_player.cpp
    src/media_manager.cpp)

set(BROMPRIS_LINUX OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(BROMPRIS_LINUX ON)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SYSTEMD REQUIRED IMPORTED_TARGET libsystemd)

    bro_dependency(brodbus GITHUB wlejon/brodbus REF 714ded627aed1c0db4b046d143bcc29e1ec9e9cf TARGET brodbus::brodbus
                   OPTIONS BRODBUS_BUILD_TESTS=OFF)

    list(APPEND BROMPRIS_SOURCES
        src/linux/mpris_client.cpp
        src/win/smtc.cpp)
elseif(WIN32)
    list(APPEND BROMPRIS_SOURCES src/win/smtc.cpp)
elseif(APPLE)
    enable_language(OBJCXX)
    list(APPEND BROMPRIS_SOURCES src/mac/now_playing.mm)
endif()

add_library(brompris STATIC ${BROMPRIS_SOURCES})
add_library(brompris::brompris ALIAS brompris)

target_include_directories(brompris
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})

brompris_warnings(brompris)

target_link_libraries(brompris PUBLIC Threads::Threads)
if(BROMPRIS_LINUX)
    target_link_libraries(brompris PUBLIC brodbus::brodbus PRIVATE PkgConfig::SYSTEMD)
endif()

# --- Standalone Bronze JavaScript API ---
option(BROMPRIS_ENABLE_API "Build Bronze JavaScript API" ${PROJECT_IS_TOP_LEVEL})
if(BROMPRIS_ENABLE_API)
    # bronze, the JavaScript runtime the api binds (it brings its code
    # generator, brass).
    bro_dependency(bronze GITHUB wlejon/bronze REF 15ce4cf5fc25d8afe03882985ae95adcba46a6a9 TARGET bronze_runtime_shared
                   OPTIONS BRONZE_BUILD_SHARED_RUNTIME=ON BRONZE_BUILD_TESTS=OFF)

    add_subdirectory(src/api)
endif()

if(BROMPRIS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
