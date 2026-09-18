cmake_minimum_required(VERSION 3.10)

project(VkInstance LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (MSVC)
    add_compile_options(/utf-8)
endif()

set(VK_WS_ROOT "${CMAKE_SOURCE_DIR}")

# Resolve shared modules from sibling OpenGL workspace or local copy.
if(EXISTS "${CMAKE_SOURCE_DIR}/../01_GLInstance/modules")
    set(OGL_WS_MODULES_DIR "${CMAKE_SOURCE_DIR}/../01_GLInstance/modules")
    set(OGL_WS_CMAKE_DIR "${CMAKE_SOURCE_DIR}/../01_GLInstance/cmake")
elseif(EXISTS "${CMAKE_SOURCE_DIR}/../GLInstance/modules")
    set(OGL_WS_MODULES_DIR "${CMAKE_SOURCE_DIR}/../GLInstance/modules")
    set(OGL_WS_CMAKE_DIR "${CMAKE_SOURCE_DIR}/../GLInstance/cmake")
elseif(EXISTS "${CMAKE_SOURCE_DIR}/modules")
    set(OGL_WS_MODULES_DIR "${CMAKE_SOURCE_DIR}/modules")
    set(OGL_WS_CMAKE_DIR "${CMAKE_SOURCE_DIR}/cmake")
else()
    message(FATAL_ERROR
        "Cannot find third-party modules.\n"
        "Expected ../01_GLInstance/modules or a local modules/ directory."
    )
endif()

find_package(Vulkan REQUIRED)

include("${CMAKE_SOURCE_DIR}/cmake/VulkanWorkspaceDependencies.cmake")
include("${CMAKE_SOURCE_DIR}/cmake/VulkanWorkspaceOutput.cmake")
include("${CMAKE_SOURCE_DIR}/cmake/VulkanWorkspaceTargets.cmake")
include("${CMAKE_SOURCE_DIR}/cmake/VulkanWorkspaceDeploy.cmake")
include("${CMAKE_SOURCE_DIR}/cmake/VulkanCompileShaders.cmake")

add_subdirectory(Vulkan_Common)
add_subdirectory(Vulkan_DemoCommon)
add_subdirectory(Vulkan_Anti_Aliasing)
add_subdirectory(Vulkan_Rendering_Paths)
add_subdirectory(Vulkan_OIT_Depth_Peeling)
add_subdirectory(Vulkan_OIT_Linked_list)
add_subdirectory(Vulkan_OIT_Stochastic_Transparency)
