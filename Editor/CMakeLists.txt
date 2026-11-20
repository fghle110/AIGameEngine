# ---------------------------------------------------------------------------
# Aegis editor. The UI and panels are compiled into a static library so that
# editor logic (command bus, panels, tools) is unit-testable; the executable is
# a thin main().
# ---------------------------------------------------------------------------
message(STATUS "Aegis: configuring editor")

file(GLOB_RECURSE AE_EDITOR_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_library(AegisEditor STATIC ${AE_EDITOR_SOURCES})
add_library(Aegis::Editor ALIAS AegisEditor)
set_target_properties(AegisEditor PROPERTIES FOLDER "Editor")
target_include_directories(AegisEditor PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(AegisEditor PUBLIC Aegis)
if(MSVC)
    target_compile_options(AegisEditor PRIVATE /bigobj)
endif()
ae_apply_warnings(AegisEditor)

add_executable(AegisEditorExe "${CMAKE_CURRENT_SOURCE_DIR}/src/EditorMain.cpp")
set_target_properties(AegisEditorExe PROPERTIES
    FOLDER "Editor"
    OUTPUT_NAME "AegisEditor")
target_link_libraries(AegisEditorExe PRIVATE AegisEditor)
ae_apply_warnings(AegisEditorExe)
