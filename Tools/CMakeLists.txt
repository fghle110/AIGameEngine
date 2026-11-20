# ---------------------------------------------------------------------------
# Command line tools used by developers and by AI agents.
# ---------------------------------------------------------------------------
message(STATUS "Aegis: configuring tools")

file(GLOB_RECURSE AE_CLI_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Cli/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Cli/*.cpp")

add_executable(AegisCli ${AE_CLI_SOURCES})
set_target_properties(AegisCli PROPERTIES FOLDER "Tools")
target_include_directories(AegisCli PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(AegisCli PRIVATE Aegis)
ae_apply_warnings(AegisCli)
