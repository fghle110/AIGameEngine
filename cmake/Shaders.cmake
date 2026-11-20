# ---------------------------------------------------------------------------
# Build-time shader compilation: GLSL -> SPIR-V via glslang.
#
# Shaders live in Engine/Shaders. Every shader compiles into
# <build>/shaders/<name>.spv and is copied next to the executables so the editor
# and an exported runtime can load them without needing a shader compiler.
# ---------------------------------------------------------------------------
set(AE_SHADER_SOURCE_DIR "${CMAKE_SOURCE_DIR}/Engine/Shaders" CACHE PATH "Shader source directory")
set(AE_SHADER_STAGE_vertex vlsl CACHE STRING "" FORCE)

function(ae_add_shaders target)
    cmake_parse_arguments(ARG "" "" "FILES" ${ARGN})
    if(NOT ARG_FILES)
        return()
    endif()

    set(_output_dir "${CMAKE_BINARY_DIR}/shaders")
    file(MAKE_DIRECTORY "${_output_dir}")
    set(_outputs)

    # GLSL sources include each other (SceneFrameConstants.glsl, ...), so every
    # shader depends on the whole shader source set; otherwise editing an include
    # would leave stale SPIR-V behind.
    file(GLOB _shader_dependencies
        "${AE_SHADER_SOURCE_DIR}/*.glsl"
        "${AE_SHADER_SOURCE_DIR}/*.vert"
        "${AE_SHADER_SOURCE_DIR}/*.frag"
        "${AE_SHADER_SOURCE_DIR}/*.comp")

    foreach(_file IN LISTS ARG_FILES)
        get_filename_component(_name "${_file}" NAME)
        set(_source "${AE_SHADER_SOURCE_DIR}/${_file}")
        set(_output "${_output_dir}/${_name}.spv")
        add_custom_command(
            OUTPUT "${_output}"
            COMMAND ${AE_GLSLANG_VALIDATOR_EXECUTABLE} -V --target-env vulkan1.2
                    "-I${AE_SHADER_SOURCE_DIR}" "${_source}" -o "${_output}"
            DEPENDS "${_source}" ${_shader_dependencies}
            COMMENT "Compiling shader ${_file}"
            VERBATIM)
        list(APPEND _outputs "${_output}")
    endforeach()

    add_custom_target(${target}_shaders DEPENDS ${_outputs})
    add_dependencies(${target} ${target}_shaders)
    set(AE_SHADER_OUTPUT_DIR "${_output_dir}" CACHE INTERNAL "Directory holding compiled shaders")
endfunction()

# Copies the compiled shaders next to an executable. Static libraries have no
# POST_BUILD step, so every executable that loads shaders calls this.
function(ae_copy_shaders target)
    if(NOT AE_SHADER_OUTPUT_DIR)
        return()
    endif()
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${AE_SHADER_OUTPUT_DIR}" "$<TARGET_FILE_DIR:${target}>/shaders"
        COMMENT "Copying compiled shaders for ${target}")
endfunction()
