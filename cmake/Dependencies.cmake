# ---------------------------------------------------------------------------
# Pinned third-party dependencies.
#
# Every dependency is pinned to an immutable tag, commit or archive hash so that
# a fresh clone builds the exact same code. Nothing is downloaded at build time
# except during CMake configure, and every archive is hash-verified.
# ---------------------------------------------------------------------------
include(FetchContent)

set(AE_DEPS_CACHE_DIR "${CMAKE_SOURCE_DIR}/.cache/deps" CACHE PATH "Shared cache for downloaded dependencies")
set(FETCHCONTENT_QUIET ${AE_VERBOSE_DEPENDENCIES})
set(FETCHCONTENT_UPDATES_DISCONNECTED OFF)

message(STATUS "Aegis: fetching third-party dependencies into ${AE_DEPS_CACHE_DIR}")

function(ae_dep_git name repository tag)
    FetchContent_Declare(${name}
        GIT_REPOSITORY ${repository}
        GIT_TAG ${tag}
        GIT_PROGRESS ${AE_VERBOSE_DEPENDENCIES}
        GIT_SHALLOW TRUE
        GIT_CONFIG "http.sslVerify=true")
endfunction()

# Downloads and extracts a source archive into <cache>/src/<name>. Used for the
# dependencies that ship no CMake build of their own.
function(ae_dep_archive name url sha256)
    set(_root "${AE_DEPS_CACHE_DIR}/src")
    set(_dest "${_root}/${name}")
    set(_stamp "${_dest}/.aegis-extracted")
    set(_archive "${AE_DEPS_CACHE_DIR}/archives/${name}.tar.gz")

    # Re-verify the archive on every configure: an interrupted download must not
    # be silently reused.
    set(_needs_download TRUE)
    if(EXISTS "${_archive}")
        file(SHA256 "${_archive}" _archive_hash)
        if(_archive_hash STREQUAL "${sha256}")
            set(_needs_download FALSE)
        else()
            message(STATUS "Aegis: discarding corrupt archive for ${name}")
            file(REMOVE "${_archive}")
        endif()
    endif()

    if(_needs_download)
        file(MAKE_DIRECTORY "${AE_DEPS_CACHE_DIR}/archives")
        message(STATUS "Aegis: downloading ${name}")
        file(DOWNLOAD "${url}" "${_archive}"
            EXPECTED_HASH SHA256=${sha256}
            TLS_VERIFY ON
            STATUS _download_status)
        list(GET _download_status 0 _download_code)
        if(NOT _download_code EQUAL 0)
            list(GET _download_status 1 _download_message)
            file(REMOVE "${_archive}")
            message(FATAL_ERROR "Aegis: failed to download ${name}: ${_download_message}")
        endif()
    endif()

    if(NOT EXISTS "${_stamp}")
        if(EXISTS "${_dest}")
            file(REMOVE_RECURSE "${_dest}")
        endif()
        set(_extract "${AE_DEPS_CACHE_DIR}/extract/${name}")
        file(REMOVE_RECURSE "${_extract}")
        file(MAKE_DIRECTORY "${_extract}")
        file(ARCHIVE_EXTRACT INPUT "${_archive}" DESTINATION "${_extract}")
        file(GLOB _children LIST_DIRECTORIES true "${_extract}/*")
        list(LENGTH _children _child_count)
        if(NOT _child_count EQUAL 1)
            file(REMOVE_RECURSE "${_extract}")
            message(FATAL_ERROR "Aegis: ${name} archive did not contain a single top-level directory")
        endif()
        list(GET _children 0 _child)
        file(MAKE_DIRECTORY "${_root}")
        file(RENAME "${_child}" "${_dest}")
        file(REMOVE_RECURSE "${_extract}")
        file(WRITE "${_stamp}" "${sha256}\n")
    endif()

    set(${name}_SOURCE_DIR "${_dest}" CACHE PATH "${name} source directory" FORCE)
endfunction()

# ---------------------------------------------------------------------------
# GLFW — windowing, input, Vulkan surface creation
# ---------------------------------------------------------------------------
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
ae_dep_git(glfw https://github.com/glfw/glfw.git 3.4)
FetchContent_MakeAvailable(glfw)
add_library(Aegis::GLFW ALIAS glfw)

# ---------------------------------------------------------------------------
# glm — math
# ---------------------------------------------------------------------------
set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_INSTALL OFF CACHE BOOL "" FORCE)
set(GLM_ENABLE_CXX_20 ON CACHE BOOL "" FORCE)
ae_dep_git(glm https://github.com/g-truc/glm.git 1.0.1)
FetchContent_MakeAvailable(glm)

# ---------------------------------------------------------------------------
# Vulkan-Headers — shared with NVRHI below
# ---------------------------------------------------------------------------
ae_dep_git(vulkan_headers https://github.com/KhronosGroup/Vulkan-Headers.git v1.4.352)
FetchContent_MakeAvailable(vulkan_headers)

# ---------------------------------------------------------------------------
# volk — Vulkan function loader (instance/device created by the engine)
# ---------------------------------------------------------------------------
ae_dep_git(volk https://github.com/zeux/volk.git 1.3.295)
FetchContent_MakeAvailable(volk)
target_link_libraries(volk PUBLIC Vulkan::Headers)
add_library(Aegis::volk ALIAS volk)

# ---------------------------------------------------------------------------
# NVRHI — RHI used by the renderer (Vulkan backend)
# ---------------------------------------------------------------------------
set(NVRHI_WITH_VULKAN ON CACHE BOOL "" FORCE)
set(NVRHI_WITH_DX11 OFF CACHE BOOL "" FORCE)
set(NVRHI_WITH_DX12 OFF CACHE BOOL "" FORCE)
set(NVRHI_WITH_VALIDATION ON CACHE BOOL "" FORCE)
set(NVRHI_WITH_NVAPI OFF CACHE BOOL "" FORCE)
set(NVRHI_INSTALL OFF CACHE BOOL "" FORCE)
set(NVRHI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(NVRHI_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(NVRHI_FETCH_VULKAN_HEADERS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(nvrhi
    GIT_REPOSITORY https://github.com/NVIDIA-RTX/NVRHI.git
    GIT_TAG bf0bbd9b666a7297492beb1999a4ddc0dfded143
    GIT_SHALLOW FALSE
    GIT_PROGRESS ${AE_VERBOSE_DEPENDENCIES})
FetchContent_MakeAvailable(nvrhi)
add_library(Aegis::NVRHI ALIAS nvrhi)
add_library(Aegis::NVRHI_Vulkan ALIAS nvrhi_vk)

# ---------------------------------------------------------------------------
# glslang — offline GLSL -> SPIR-V compilation at build time
# ---------------------------------------------------------------------------
set(ENABLE_GLSLANG_BINARIES ON CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_JS OFF CACHE BOOL "" FORCE)
set(ENABLE_HLSL ON CACHE BOOL "" FORCE)
set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
set(ENABLE_SPVREMAPPER OFF CACHE BOOL "" FORCE)
set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(SKIP_GLSLANG_INSTALL ON CACHE BOOL "" FORCE)
set(BUILD_EXTERNAL OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
# glslang's standalone validator generates its intrinsic header with Python. The
# FindPython3 imported target only declares the standard configurations, which
# breaks generation for the custom "Dist" configuration. Declare a global,
# configuration-independent interpreter target first so FindPython3 reuses it.
if(NOT TARGET Python3::Interpreter)
    # The plain "python" name may be missing while the Windows launcher (py.exe)
    # exists, so resolve the real interpreter through whichever launcher is found.
    find_program(AE_PYTHON3_LAUNCHER NAMES python3 python py)
    set(_aegis_python_ok FALSE)
    if(AE_PYTHON3_LAUNCHER)
        execute_process(COMMAND "${AE_PYTHON3_LAUNCHER}" -c "import sys; print(sys.executable)"
            OUTPUT_VARIABLE _aegis_python_path
            OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE _aegis_python_result
            ERROR_QUIET)
        if(_aegis_python_result EQUAL 0 AND EXISTS "${_aegis_python_path}")
            set(_aegis_python_ok TRUE)
        endif()
    endif()
    if(_aegis_python_ok)
        add_executable(Python3::Interpreter IMPORTED GLOBAL)
        set_target_properties(Python3::Interpreter PROPERTIES
            IMPORTED_LOCATION "${_aegis_python_path}")
        # MAP_IMPORTED_CONFIG_DIST makes CMake resolve the Dist configuration
        # through a mapped standard one, so every mapped location must exist.
        foreach(_aegis_config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
            set_property(TARGET Python3::Interpreter PROPERTY IMPORTED_LOCATION_${_aegis_config}
                "${_aegis_python_path}")
        endforeach()
        message(STATUS "Aegis: using ${_aegis_python_path} for glslang source generation")
    else()
        message(FATAL_ERROR "Aegis: no working Python 3 interpreter found; glslang needs one to generate its intrinsic header")
    endif()
endif()

ae_dep_git(glslang https://github.com/KhronosGroup/glslang.git 15.4.0)
FetchContent_MakeAvailable(glslang)

# FindPython3 may only add per-configuration locations when it finds Python
# itself. Our global target carries a single location; keep it authoritative for
# every configuration (including the custom Dist one) by dropping the
# configuration list after glslang has configured.
if(TARGET Python3::Interpreter AND _aegis_python_ok)
    set_property(TARGET Python3::Interpreter PROPERTY IMPORTED_LOCATION "${_aegis_python_path}")
    set_property(TARGET Python3::Interpreter PROPERTY IMPORTED_CONFIGURATIONS "")
    foreach(_aegis_config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
        set_property(TARGET Python3::Interpreter PROPERTY IMPORTED_LOCATION_${_aegis_config}
            "${_aegis_python_path}")
    endforeach()
endif()


if(TARGET glslang-standalone)
    set(AE_GLSLANG_VALIDATOR glslang-standalone CACHE STRING "glslang validator target" FORCE)
elseif(TARGET glslangValidator)
    set(AE_GLSLANG_VALIDATOR glslangValidator CACHE STRING "glslang validator target" FORCE)
else()
    message(FATAL_ERROR "Aegis: glslang build did not produce a shader validator target")
endif()
set(AE_GLSLANG_VALIDATOR_EXECUTABLE "$<TARGET_FILE:${AE_GLSLANG_VALIDATOR}>" CACHE STRING "glslang validator executable" FORCE)

# ---------------------------------------------------------------------------
# Jolt Physics — rigid body dynamics
# ---------------------------------------------------------------------------
set(TARGET_SAMPLES OFF CACHE BOOL "" FORCE)
set(TARGET_VIEWER OFF CACHE BOOL "" FORCE)
set(TARGET_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
set(TARGET_HELLO_WORLD OFF CACHE BOOL "" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
set(USE_ASSERTS ON CACHE BOOL "" FORCE)
set(CROSS_PLATFORM_DETERMINISTIC OFF CACHE BOOL "" FORCE)
FetchContent_Declare(jolt
    GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
    GIT_TAG v5.7.0
    GIT_SHALLOW TRUE
    GIT_PROGRESS ${AE_VERBOSE_DEPENDENCIES}
    SOURCE_SUBDIR Build)
FetchContent_MakeAvailable(jolt)
add_library(Aegis::Jolt ALIAS Jolt)

# ---------------------------------------------------------------------------
# doctest — unit test framework
# ---------------------------------------------------------------------------
set(DOCTEST_WITH_TESTS OFF CACHE BOOL "" FORCE)
set(DOCTEST_WITH_INSTALL OFF CACHE BOOL "" FORCE)
ae_dep_git(doctest https://github.com/doctest/doctest.git v2.4.12)
FetchContent_MakeAvailable(doctest)

# ---------------------------------------------------------------------------
# Source-only dependencies
# ---------------------------------------------------------------------------
ae_dep_archive(imgui
    "https://codeload.github.com/ocornut/imgui/tar.gz/refs/tags/v1.91.9b-docking"
    "466fdef9b18de15f0bb6e288e3d00ffa3d82200ec458ce5e4f724a161d9528a5")
ae_dep_archive(lua
    "https://www.lua.org/ftp/lua-5.4.8.tar.gz"
    "4f18ddae154e793e46eeab727c59ef1c0c0c2b744e7b94219710d76f530629ae")
ae_dep_archive(stb
    "https://codeload.github.com/nothings/stb/tar.gz/6b5799bae8a1fe3792691ef4b72ae26434c3e799"
    "87e9ecd2d2e0cb8fa35bf5a93ea6ca3999595ea4f0a4738ed1aac0a34cbd4da9")
ae_dep_archive(cgltf
    "https://codeload.github.com/jkuhlmann/cgltf/tar.gz/refs/tags/v1.15"
    "84e352092e5cd6aab7f66de62ddb66beb5e6f18d412ca9d12950d7a55bfef25a")
ae_dep_archive(miniaudio
    "https://codeload.github.com/mackron/miniaudio/tar.gz/refs/tags/0.11.26"
    "838ba55a21c9142adf7b76bdc4351e5cd8b7ae128f46b25ec25f94ae9969c600")

# ---------------------------------------------------------------------------
# Lua — static library
# ---------------------------------------------------------------------------
file(GLOB AE_LUA_SOURCES "${lua_SOURCE_DIR}/src/*.c")
list(FILTER AE_LUA_SOURCES EXCLUDE REGEX "(lua|luac|onelua)\\.c$")
add_library(lua STATIC ${AE_LUA_SOURCES})
target_include_directories(lua PUBLIC "${lua_SOURCE_DIR}/src")
if(WIN32)
    target_compile_definitions(lua PUBLIC LUA_USE_WINDOWS=1)
else()
    target_compile_definitions(lua PUBLIC LUA_USE_POSIX=1)
endif()
set_target_properties(lua PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON POSITION_INDEPENDENT_CODE ON)
add_library(Aegis::Lua ALIAS lua)

# ---------------------------------------------------------------------------
# Single-header libraries: interface target + one implementation TU
# ---------------------------------------------------------------------------
file(MAKE_DIRECTORY "${CMAKE_SOURCE_DIR}/third_party/impl")

add_library(ae_stb INTERFACE)
target_include_directories(ae_stb INTERFACE "${stb_SOURCE_DIR}")
target_sources(ae_stb INTERFACE "${CMAKE_SOURCE_DIR}/third_party/impl/StbImpl.c")
add_library(Aegis::STB ALIAS ae_stb)

add_library(ae_cgltf INTERFACE)
target_include_directories(ae_cgltf INTERFACE "${cgltf_SOURCE_DIR}")
target_sources(ae_cgltf INTERFACE "${CMAKE_SOURCE_DIR}/third_party/impl/CgltfImpl.c")
add_library(Aegis::cgltf ALIAS ae_cgltf)

add_library(ae_miniaudio INTERFACE)
target_include_directories(ae_miniaudio INTERFACE "${miniaudio_SOURCE_DIR}")
target_sources(ae_miniaudio INTERFACE "${CMAKE_SOURCE_DIR}/third_party/impl/MiniaudioImpl.c")
if(WIN32)
    target_link_libraries(ae_miniaudio INTERFACE ole32 uuid)
else()
    target_link_libraries(ae_miniaudio INTERFACE dl pthread m)
endif()
add_library(Aegis::miniaudio ALIAS ae_miniaudio)

message(STATUS "Aegis: dependencies ready")
