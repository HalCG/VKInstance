# =============================================================================
# VulkanWorkspaceTargets.cmake — 子项目共用 target 辅助函数
# =============================================================================

if(POLICY CMP0175)
    cmake_policy(SET CMP0175 OLD)
endif()
#
# vk_ws_set_run_output(target) — exe 输出到 run/Debug|Release/<target>/（按配置分目录）
# vk_ws_link_assimp(target)     — 链接 Assimp + POST_BUILD 拷贝运行时 DLL
#
# =============================================================================

# Debug → run/Debug/<TargetName>/；Release 及其它优化配置 → run/Release/<TargetName>/
function(_vk_ws_run_subdir_for_config cfg_name out_var)
    if(cfg_name STREQUAL "Debug")
        set(${out_var} "Debug" PARENT_SCOPE)
    else()
        set(${out_var} "Release" PARENT_SCOPE)
    endif()
endfunction()

function(vk_ws_set_run_output target_name)
    if(CMAKE_CONFIGURATION_TYPES)
        foreach(_cfg ${CMAKE_CONFIGURATION_TYPES})
            string(TOUPPER ${_cfg} _cfg_upper)
            _vk_ws_run_subdir_for_config("${_cfg}" _subdir)
            set(_run_dir "${VK_WS_ROOT}/run/${_subdir}/${target_name}")
            set_target_properties(${target_name} PROPERTIES
                RUNTIME_OUTPUT_DIRECTORY_${_cfg_upper} "${_run_dir}"
                VS_DEBUGGER_WORKING_DIRECTORY_${_cfg_upper} "${_run_dir}")
        endforeach()
    else()
        if(CMAKE_BUILD_TYPE STREQUAL "Debug")
            set(_subdir "Debug")
        else()
            set(_subdir "Release")
        endif()
        set(_run_dir "${VK_WS_ROOT}/run/${_subdir}/${target_name}")
        set_target_properties(${target_name} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${_run_dir}"
            VS_DEBUGGER_WORKING_DIRECTORY "${_run_dir}")
    endif()
endfunction()

# 拷贝 Assimp 传递依赖 DLL（与 OpenGL ogl_ws_deploy_runtime 相同源目录）
# Debug:  */debug/bin → assimp-vc143-mtd.dll, zlibd1.dll, ...
# Release: */bin      → assimp-vc143-mt.dll, zlib1.dll, ...
function(vk_ws_deploy_module_dlls target_name)
    if(NOT WIN32)
        return()
    endif()

    set(_copy_script "${CMAKE_SOURCE_DIR}/cmake/CopyModuleDlls.cmake")
    string(REPLACE ";" "\\;" _MODULE_DLL_ROOTS_ESC "${VK_WS_MODULE_DLL_ROOTS}")

    if(CMAKE_CONFIGURATION_TYPES)
        # VS 生成器会把多条 CONFIGURATIONS POST_BUILD 都塞进同一配置；用 genex 一次搞定
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND}
                -Droots=${_MODULE_DLL_ROOTS_ESC}
                -Dbinsub=$<IF:$<CONFIG:Debug>,debug/bin,bin>
                -Doutdir=$<TARGET_FILE_DIR:${target_name}>
                -P "${_copy_script}"
            COMMENT "Copy runtime DLLs from modules"
        )
    else()
        if(CMAKE_BUILD_TYPE STREQUAL "Debug")
            set(_binsub "debug/bin")
        else()
            set(_binsub "bin")
        endif()
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND}
                -Droots=${_MODULE_DLL_ROOTS_ESC}
                -Dbinsub=${_binsub}
                -Doutdir=$<TARGET_FILE_DIR:${target_name}>
                -P "${_copy_script}"
            COMMENT "Copy runtime DLLs from modules"
        )
    endif()
endfunction()

function(vk_ws_link_assimp target_name)
    set(_assimp_root "${VK_WS_ASSIMP_ROOT}")
    set(_assimp_lib "${_assimp_root}/lib/assimp-vc143-mt.lib")

    if(NOT EXISTS "${_assimp_root}/include/assimp/Importer.hpp")
        message(FATAL_ERROR "Assimp headers not found under ${_assimp_root}")
    endif()
    if(NOT EXISTS "${_assimp_lib}")
        message(FATAL_ERROR "Assimp import library not found: ${_assimp_lib}")
    endif()

    target_include_directories(${target_name} PRIVATE "${_assimp_root}/include")
    target_link_libraries(${target_name} PRIVATE "${_assimp_lib}")
    vk_ws_deploy_module_dlls(${target_name})
endfunction()

# 可选：若使用 GLFW DLL 版（MinGW 等），拷贝 glfw3.dll
function(vk_ws_deploy_glfw_dll target_name)
    if(GLFW3_DLL AND EXISTS "${GLFW3_DLL}")
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${GLFW3_DLL}"
                "$<TARGET_FILE_DIR:${target_name}>"
            COMMENT "Deploy GLFW DLL next to ${target_name}"
        )
    endif()
endfunction()
