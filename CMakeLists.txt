cmake_minimum_required(VERSION 3.24)

project(brompris VERSION 0.1.0 LANGUAGES CXX)

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

    if(NOT TARGET brodbus::brodbus)
        if(DEFINED BRODBUS_DIR AND EXISTS "${BRODBUS_DIR}/CMakeLists.txt")
            # Explicit BRODBUS_DIR supplied
        elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../brodbus/CMakeLists.txt")
            set(BRODBUS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../brodbus" CACHE PATH "brodbus source tree")
        elseif(EXISTS "${CMAKE_SOURCE_DIR}/../brodbus/CMakeLists.txt")
            set(BRODBUS_DIR "${CMAKE_SOURCE_DIR}/../brodbus" CACHE PATH "brodbus source tree")
        elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/brodbus/CMakeLists.txt")
            set(BRODBUS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/brodbus" CACHE PATH "brodbus source tree")
        elseif(EXISTS "${CMAKE_SOURCE_DIR}/third_party/brodbus/CMakeLists.txt")
            set(BRODBUS_DIR "${CMAKE_SOURCE_DIR}/third_party/brodbus" CACHE PATH "brodbus source tree")
        endif()

        if(NOT BRODBUS_DIR OR NOT EXISTS "${BRODBUS_DIR}/CMakeLists.txt")
            message(FATAL_ERROR
                "brompris needs brodbus: not found at ${BRODBUS_DIR} or "
                "${CMAKE_SOURCE_DIR}/third_party/brodbus. Clone https://github.com/wlejon/brodbus next "
                "to this repo, run `git submodule update --init --recursive`, or override with "
                "-DBRODBUS_DIR=...")
        endif()

        set(BRODBUS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        message(STATUS "brompris: brodbus from ${BRODBUS_DIR}")
        add_subdirectory("${BRODBUS_DIR}" "${CMAKE_BINARY_DIR}/brodbus" EXCLUDE_FROM_ALL)
    endif()

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
    if(NOT TARGET bronze_runtime_shared)
        set(BRONZE_BUILD_SHARED_RUNTIME ON CACHE BOOL "" FORCE)
        set(BRONZE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        if(DEFINED BRONZE_DIR AND EXISTS "${BRONZE_DIR}/CMakeLists.txt")
            # Explicit BRONZE_DIR supplied
        elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../bronze/CMakeLists.txt")
            set(BRONZE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../bronze" CACHE PATH "bronze source tree")
        elseif(EXISTS "${CMAKE_SOURCE_DIR}/../bronze/CMakeLists.txt")
            set(BRONZE_DIR "${CMAKE_SOURCE_DIR}/../bronze" CACHE PATH "bronze source tree")
        endif()

        if(NOT BRONZE_DIR OR NOT EXISTS "${BRONZE_DIR}/CMakeLists.txt")
            message(FATAL_ERROR
                "bronze not found: check it out beside this repository (../bronze, with ../brass beside it) "
                "or pass -DBRONZE_DIR=<path>.")
        endif()

        add_subdirectory("${BRONZE_DIR}" "${CMAKE_BINARY_DIR}/bronze-build" EXCLUDE_FROM_ALL)
    endif()

    add_subdirectory(src/api)
endif()

if(BROMPRIS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
