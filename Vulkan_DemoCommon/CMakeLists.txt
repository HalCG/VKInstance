cmake_minimum_required(VERSION 3.10)

project(Vulkan_DemoCommon LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(Vulkan_DemoCommon STATIC
    src/DemoApp.cpp
    src/DemoMesh.cpp
    src/DemoRhi.cpp
)

target_include_directories(Vulkan_DemoCommon PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${VK_WS_ROOT}/Vulkan_Common/include
    ${Vulkan_INCLUDE_DIRS}
    ${GLFW3_INCLUDE_DIR}
    ${VK_WS_GLM_INCLUDE_DIR}
)

target_link_libraries(Vulkan_DemoCommon PUBLIC
    Vulkan_Common
    Vulkan::Vulkan
    ${GLFW3_LIBRARY}
)
