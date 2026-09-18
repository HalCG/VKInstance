cmake_minimum_required(VERSION 3.10)

project(Vulkan_Anti_Aliasing LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(Vulkan_Anti_Aliasing
    src/main.cpp
    src/AntiAliasingDemo.cpp
)
vk_ws_set_run_output(Vulkan_Anti_Aliasing)

target_include_directories(Vulkan_Anti_Aliasing PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${VK_WS_ROOT}/Vulkan_DemoCommon/include
    ${VK_WS_ROOT}/Vulkan_Common/include
    ${Vulkan_INCLUDE_DIRS}
    ${GLFW3_INCLUDE_DIR}
    ${VK_WS_GLM_INCLUDE_DIR}
    "${OGL_WS_MODULES_DIR}/stb_image"
)

target_link_libraries(Vulkan_Anti_Aliasing PRIVATE
    Vulkan_DemoCommon
    Vulkan_Common
    Vulkan::Vulkan
    ${GLFW3_LIBRARY}
)
vk_ws_link_assimp(Vulkan_Anti_Aliasing)

vk_ws_compile_shaders(Vulkan_Anti_Aliasing
    shaders/mesh_msaa.vert
    shaders/mesh_msaa.frag
    shaders/fxaa.vert
    shaders/fxaa.frag
    shaders/taa.vert
    shaders/taa.frag
)
vk_ws_deploy_runtime(Vulkan_Anti_Aliasing)

include("${CMAKE_SOURCE_DIR}/cmake/VulkanWorkspaceResources.cmake")
vk_ws_deploy_static_resources(Vulkan_Anti_Aliasing)
