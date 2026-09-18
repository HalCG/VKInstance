cmake_minimum_required(VERSION 3.10)

project(Vulkan_Rendering_Paths LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(Vulkan_Rendering_Paths
    src/main.cpp
    src/RenderingPathsApp.cpp
    src/Scene.cpp
    src/LightManager.cpp
    src/DeferredRenderer.cpp
    src/ForwardRenderer.cpp
    src/ForwardPlusRenderer.cpp
    src/VulkanRhi.cpp
)
vk_ws_set_run_output(Vulkan_Rendering_Paths)

target_include_directories(Vulkan_Rendering_Paths PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${VK_WS_ROOT}/Vulkan_Common/include
    ${Vulkan_INCLUDE_DIRS}
    ${GLFW3_INCLUDE_DIR}
    ${VK_WS_GLM_INCLUDE_DIR}
    "${OGL_WS_MODULES_DIR}/stb_image"
)

target_link_libraries(Vulkan_Rendering_Paths PRIVATE
    Vulkan_Common
    Vulkan::Vulkan
    ${GLFW3_LIBRARY}
)
vk_ws_link_assimp(Vulkan_Rendering_Paths)

vk_ws_compile_shaders(Vulkan_Rendering_Paths
    shaders/mesh.vert
    shaders/geometry.frag
    shaders/forward.frag
    shaders/forward_plus.frag
    shaders/deferred_lighting.vert
    shaders/deferred_lighting.frag
    shaders/gbuffer_debug.frag
)
vk_ws_deploy_runtime(Vulkan_Rendering_Paths)

include("${CMAKE_SOURCE_DIR}/cmake/VulkanWorkspaceResources.cmake")
vk_ws_deploy_static_resources(Vulkan_Rendering_Paths)
