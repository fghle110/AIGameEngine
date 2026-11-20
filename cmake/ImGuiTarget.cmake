# ---------------------------------------------------------------------------
# Dear ImGui (docking branch), built with the GLFW platform backend only.
# Rendering is done by the engine itself through NVRHI (see ImGuiRenderer).
# ---------------------------------------------------------------------------
set(_imgui "${imgui_SOURCE_DIR}")
if(NOT EXISTS "${_imgui}/imgui.cpp")
    message(FATAL_ERROR "Aegis: Dear ImGui sources not found at ${_imgui}")
endif()

add_library(imgui STATIC
    "${_imgui}/imgui.cpp"
    "${_imgui}/imgui_draw.cpp"
    "${_imgui}/imgui_tables.cpp"
    "${_imgui}/imgui_widgets.cpp"
    "${_imgui}/imgui_demo.cpp"
    "${_imgui}/backends/imgui_impl_glfw.cpp")

target_include_directories(imgui PUBLIC
    "${_imgui}"
    "${_imgui}/backends")
target_link_libraries(imgui PUBLIC glfw)
target_compile_definitions(imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS=1)
set_target_properties(imgui PROPERTIES FOLDER "ThirdParty")

add_library(Aegis::ImGui ALIAS imgui)
