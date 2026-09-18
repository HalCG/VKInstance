cmake_minimum_required(VERSION 3.10)

project(Vulkan_Common LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(Vulkan_Common STATIC
    src/VulkanContext.cpp
    src/VulkanBuffer.cpp
    src/VulkanShader.cpp
)

target_include_directories(Vulkan_Common PUBLIC
    include
    ${Vulkan_INCLUDE_DIRS}
    ${GLFW3_INCLUDE_DIR}
    ${VK_WS_GLM_INCLUDE_DIR}
)

target_compile_definitions(Vulkan_Common PUBLIC
    VK_WS_GLSLC_EXECUTABLE="${Vulkan_GLSLC_EXECUTABLE}"
)

target_link_libraries(Vulkan_Common PUBLIC
    Vulkan::Vulkan
    ${GLFW3_LIBRARY}
)
