cmake_minimum_required(VERSION 3.24)

# Aegis — a production-grade 3D game engine (Windows / macOS / Linux).
# Design notes, conventions and the development guide live in AGENTS.md and docs/.
project(Aegis
    VERSION 0.1.0
    DESCRIPTION "Aegis 3D game engine"
    HOMEPAGE_URL "https://github.com/fghle110/AIGameEngine"
    LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# ---------------------------------------------------------------------------
# Global build settings
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo Dist)
endif()

# A dedicated "Dist" configuration produces the shipping runtime (see docs/BUILDING.md).
set(CMAKE_CONFIGURATION_TYPES "Debug;Release;RelWithDebInfo;Dist" CACHE STRING "Available build configurations" FORCE)
set(CMAKE_MAP_IMPORTED_CONFIG_DIST "Release;RelWithDebInfo;Debug" CACHE STRING "" FORCE)
set(CMAKE_C_FLAGS_DIST "${CMAKE_C_FLAGS_RELEASE}" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS_DIST "${CMAKE_CXX_FLAGS_RELEASE}" CACHE STRING "" FORCE)
set(CMAKE_EXE_LINKER_FLAGS_DIST "${CMAKE_EXE_LINKER_FLAGS_RELEASE}" CACHE STRING "" FORCE)
set(CMAKE_SHARED_LINKER_FLAGS_DIST "${CMAKE_SHARED_LINKER_FLAGS_RELEASE}" CACHE STRING "" FORCE)

# Keep every artifact in a predictable place.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
foreach(_cfg IN ITEMS DEBUG RELEASE RELWITHDEBINFO DIST)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${_cfg} "${CMAKE_BINARY_DIR}/bin")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${_cfg} "${CMAKE_BINARY_DIR}/lib")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${_cfg} "${CMAKE_BINARY_DIR}/lib")
endforeach()

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(AE_WARNINGS_AS_ERRORS "Treat compiler warnings in engine code as errors" OFF)
option(AE_BUILD_TESTS "Build the Aegis test suite" ON)
option(AE_BUILD_EDITOR "Build the Aegis editor" ON)
option(AE_BUILD_RUNTIME "Build the Aegis runtime (game player)" ON)
option(AE_BUILD_TOOLS "Build Aegis command line tools" ON)
option(AE_VERBOSE_DEPENDENCIES "Show dependency fetch output" OFF)

if(AE_WARNINGS_AS_ERRORS)
    add_compile_definitions(AE_WARNINGS_AS_ERRORS=1)
endif()

# ---------------------------------------------------------------------------
# Third-party dependencies (pinned; see cmake/Dependencies.cmake)
# ---------------------------------------------------------------------------
include(Dependencies)
include(CompilerWarnings)
include(Shaders)
include(ImGuiTarget)

# ---------------------------------------------------------------------------
# Engine, editor, runtime, tools, tests
# ---------------------------------------------------------------------------
add_subdirectory(Engine)

if(AE_BUILD_EDITOR)
    add_subdirectory(Editor)
endif()

if(AE_BUILD_RUNTIME)
    add_subdirectory(Runtime)
endif()

if(AE_BUILD_TOOLS)
    add_subdirectory(Tools)
endif()

if(AE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(Tests)
endif()
