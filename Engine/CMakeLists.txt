# ---------------------------------------------------------------------------
# Aegis engine library (static). Holds the runtime: core, platform, ECS, scene,
# renderer, assets, scripting, physics and audio.
# ---------------------------------------------------------------------------
message(STATUS "Aegis: configuring engine")

file(GLOB_RECURSE AE_ENGINE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Aegis/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Aegis/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Aegis/*.inl")
list(APPEND AE_ENGINE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/Aegis.h")

foreach(_excluded IN LISTS AE_EXCLUDE_MODULES)
    list(FILTER AE_ENGINE_SOURCES EXCLUDE REGEX "/Aegis/${_excluded}/")
endforeach()

add_library(Aegis STATIC ${AE_ENGINE_SOURCES})
add_library(Aegis::Engine ALIAS Aegis)

set_target_properties(Aegis PROPERTIES FOLDER "Aegis")

target_include_directories(Aegis PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_BINARY_DIR}/shaders")

target_link_libraries(Aegis
    PUBLIC
        Aegis::GLFW
        glm::glm
        Aegis::volk
        Aegis::NVRHI
        Aegis::NVRHI_Vulkan
        Aegis::Lua
        Aegis::ImGui
        Vulkan::Headers
    PRIVATE
        Aegis::Jolt
        Aegis::STB
        Aegis::cgltf
        Aegis::miniaudio)

if(WIN32)
    target_link_libraries(Aegis PRIVATE ws2_32 dbghelp)
else()
    target_link_libraries(Aegis PRIVATE dl pthread)
endif()

target_compile_definitions(Aegis PUBLIC VK_NO_PROTOTYPES=1)

if(WIN32)
    target_compile_definitions(Aegis PUBLIC AE_PLATFORM_WINDOWS=1 AE_PLATFORM_LINUX=0 AE_PLATFORM_MACOS=0)
    target_compile_definitions(Aegis PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
elseif(APPLE)
    target_compile_definitions(Aegis PUBLIC AE_PLATFORM_WINDOWS=0 AE_PLATFORM_LINUX=0 AE_PLATFORM_MACOS=1)
else()
    target_compile_definitions(Aegis PUBLIC AE_PLATFORM_WINDOWS=0 AE_PLATFORM_LINUX=1 AE_PLATFORM_MACOS=0)
endif()

target_compile_definitions(Aegis PUBLIC GLM_ENABLE_EXPERIMENTAL=1 GLM_FORCE_DEPTH_ZERO_TO_ONE=1)
target_compile_definitions(Aegis PUBLIC
    $<$<CONFIG:Debug>:AE_DEBUG=1>
    $<$<CONFIG:Release>:AE_RELEASE=1>
    $<$<CONFIG:RelWithDebInfo>:AE_RELEASE=1>
    $<$<CONFIG:Dist>:AE_RELEASE=1>
    $<$<CONFIG:Dist>:AE_DIST=1>)

if(MSVC)
    target_compile_options(Aegis PRIVATE /bigobj)
    target_compile_definitions(Aegis PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

ae_add_shaders(Aegis FILES ImGui.vert ImGui.frag)

ae_apply_warnings(Aegis)
