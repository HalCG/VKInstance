# Build-time GLSL -> SPIR-V compilation via Vulkan SDK glslc.

function(vk_ws_compile_shaders target_name)
    if(NOT Vulkan_GLSLC_EXECUTABLE)
        message(FATAL_ERROR "Vulkan_GLSLC_EXECUTABLE not found. Install the Vulkan SDK.")
    endif()

    set(_shader_outputs "")
    foreach(_shader ${ARGN})
        if(IS_ABSOLUTE "${_shader}")
            set(_shader_path "${_shader}")
        else()
            set(_shader_path "${CMAKE_CURRENT_SOURCE_DIR}/${_shader}")
        endif()

        if(NOT EXISTS "${_shader_path}")
            message(FATAL_ERROR "Shader source not found: ${_shader_path}")
        endif()

        get_filename_component(_shader_stem "${_shader_path}" NAME_WE)
        get_filename_component(_shader_ext "${_shader_path}" EXT)
        string(SUBSTRING "${_shader_ext}" 1 -1 _shader_stage)
        set(_shader_name "${_shader_stem}_${_shader_stage}")
        set(_output_spv "${CMAKE_CURRENT_BINARY_DIR}/resources/shaders/${_shader_name}.spv")

        add_custom_command(
            OUTPUT "${_output_spv}"
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "${CMAKE_CURRENT_BINARY_DIR}/resources/shaders"
            COMMAND "${Vulkan_GLSLC_EXECUTABLE}" "${_shader_path}" -o "${_output_spv}"
            DEPENDS "${_shader_path}"
            COMMENT "Compiling SPIR-V ${_shader_name}"
            VERBATIM
        )
        list(APPEND _shader_outputs "${_output_spv}")
    endforeach()

    if(_shader_outputs)
        add_custom_target(${target_name}_shaders DEPENDS ${_shader_outputs})
        add_dependencies(${target_name} ${target_name}_shaders)
    endif()
endfunction()
