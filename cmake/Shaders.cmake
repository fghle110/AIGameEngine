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

    foreach(_file IN LISTS ARG_FILES)
        get_filename_component(_name "${_file}" NAME_WE)
        set(_source "${AE_SHADER_SOURCE_DIR}/${_file}")
        set(_output "${_output_dir}/${_name}.spv")
        add_custom_command(
            OUTPUT "${_output}"
            COMMAND ${AE_GLSLANG_VALIDATOR_EXECUTABLE} -V --target-env vulkan1.2
                    -I "${AE_SHADER_SOURCE_DIR}" "${_source}" -o "${_output}"
            DEPENDS "${_source}"
            COMMENT "Compiling shader ${_file}"
            VERBATIM)
        list(APPEND _outputs "${_output}")
    endforeach()

    add_custom_target(${target}_shaders DEPENDS ${_outputs})
    add_dependencies(${target} ${target}_shaders)

    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${_output_dir}" "$<TARGET_FILE_DIR:${target}>/shaders"
        COMMENT "Copying compiled shaders for ${target}")
endfunction()
