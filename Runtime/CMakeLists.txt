# ---------------------------------------------------------------------------
# Aegis runtime: runs an exported game. Contains no editor code and never links
# ImGui-based editor sources.
# ---------------------------------------------------------------------------
message(STATUS "Aegis: configuring runtime")

file(GLOB_RECURSE AE_RUNTIME_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_executable(AegisRuntime ${AE_RUNTIME_SOURCES})
set_target_properties(AegisRuntime PROPERTIES FOLDER "Runtime")
target_include_directories(AegisRuntime PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(AegisRuntime PRIVATE Aegis)
ae_add_shaders(AegisRuntime FILES Blit.frag)
ae_copy_shaders(AegisRuntime)
ae_apply_warnings(AegisRuntime)
