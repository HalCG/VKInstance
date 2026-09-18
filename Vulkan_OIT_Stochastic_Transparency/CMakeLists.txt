cmake_minimum_required(VERSION 3.10)

project(Vulkan_OIT_Stochastic_Transparency LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(Vulkan_OIT_Stochastic_Transparency
    src/main.cpp
    src/StochasticDemo.cpp
    src/OitScene.cpp
    "${OGL_WS_MODULES_DIR}/stb_image/stb_image_wrap.cpp"
)
vk_ws_set_run_output(Vulkan_OIT_Stochastic_Transparency)

target_include_directories(Vulkan_OIT_Stochastic_Transparency PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${VK_WS_ROOT}/Vulkan_DemoCommon/include
    ${VK_WS_ROOT}/Vulkan_Common/include
    ${Vulkan_INCLUDE_DIRS}
    ${GLFW3_INCLUDE_DIR}
    ${VK_WS_GLM_INCLUDE_DIR}
    "${OGL_WS_MODULES_DIR}/stb_image"
)

target_link_libraries(Vulkan_OIT_Stochastic_Transparency PRIVATE
    Vulkan_DemoCommon
    Vulkan_Common
    Vulkan::Vulkan
    ${GLFW3_LIBRARY}
)
vk_ws_link_assimp(Vulkan_OIT_Stochastic_Transparency)

vk_ws_compile_shaders(Vulkan_OIT_Stochastic_Transparency
    shaders/stochastic.vert
    shaders/stochastic.frag
)
vk_ws_deploy_runtime(Vulkan_OIT_Stochastic_Transparency)

include("${CMAKE_SOURCE_DIR}/cmake/VulkanWorkspaceResources.cmake")
vk_ws_deploy_static_resources(Vulkan_OIT_Stochastic_Transparency)
