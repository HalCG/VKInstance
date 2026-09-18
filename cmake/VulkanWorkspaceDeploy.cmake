# Post-build: copy compiled resources next to the executable.

function(vk_ws_deploy_runtime target_name)
    set_target_properties(${target_name} PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:${target_name}>"
    )

    add_custom_command(TARGET ${target_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -DSRC="${CMAKE_CURRENT_BINARY_DIR}/resources"
            -DDST="$<TARGET_FILE_DIR:${target_name}>/resources"
            -P "${CMAKE_SOURCE_DIR}/cmake/CopyResourcesIfExists.cmake"
        COMMENT "Deploy resources next to ${target_name}"
    )
endfunction()
