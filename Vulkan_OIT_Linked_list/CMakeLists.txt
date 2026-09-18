cmake_minimum_required(VERSION 3.10)

# Vulkan OIT Linked List — 三 Pass PPLL 演示
# 详见 README.md；SPIR-V 与 resources 在 POST_BUILD 部署到 exe 目录

project(Vulkan_OIT_Linked_list LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(Vulkan_OIT_Linked_list
    src/main.cpp
    src/LinkedListDemo.cpp
    src/OitScene.cpp
    "${OGL_WS_MODULES_DIR}/stb_image/stb_image_wrap.cpp"
)
vk_ws_set_run_output(Vulkan_OIT_Linked_list)

target_include_directories(Vulkan_OIT_Linked_list PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${VK_WS_ROOT}/Vulkan_DemoCommon/include
    ${VK_WS_ROOT}/Vulkan_Common/include
    ${Vulkan_INCLUDE_DIRS}
    ${GLFW3_INCLUDE_DIR}
    ${VK_WS_GLM_INCLUDE_DIR}
    "${OGL_WS_MODULES_DIR}/stb_image"
)

target_link_libraries(Vulkan_OIT_Linked_list PRIVATE
    Vulkan_DemoCommon
    Vulkan_Common
    Vulkan::Vulkan
    ${GLFW3_LIBRARY}
)
vk_ws_link_assimp(Vulkan_OIT_Linked_list)

vk_ws_compile_shaders(Vulkan_OIT_Linked_list
    shaders/lit.vert
    shaders/blinn_phong.frag
    shaders/oit_build.vert
    shaders/oit_build.frag
    shaders/composite.vert
    shaders/composite.frag
)
vk_ws_deploy_runtime(Vulkan_OIT_Linked_list)

include("${CMAKE_SOURCE_DIR}/cmake/VulkanWorkspaceResources.cmake")
vk_ws_deploy_static_resources(Vulkan_OIT_Linked_list)
