# Shared third-party dependencies for all Vulkan workspace subprojects.
# Included from the root CMakeLists.txt only.

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${OGL_WS_MODULES_DIR}/cmake")
find_package(GLFW3 REQUIRED)

set(VK_WS_GLM_INCLUDE_DIR "${OGL_WS_MODULES_DIR}/glm-1.0.1")
if(NOT EXISTS "${VK_WS_GLM_INCLUDE_DIR}/glm/glm.hpp")
    message(FATAL_ERROR
        "GLM not found at ${VK_WS_GLM_INCLUDE_DIR}\n"
        "Ensure sibling OpenGL workspace modules are available "
        "(../01_GLInstance/modules) or copy modules/ into this repository."
    )
endif()

# Assimp 及其 vcpkg 传递依赖 — POST_BUILD 从各模块 bin/ 或 debug/bin 拷贝 DLL
set(VK_WS_ASSIMP_ROOT "${OGL_WS_MODULES_DIR}/assimp_x64-windows")
set(VK_WS_MODULE_DLL_ROOTS
    "${VK_WS_ASSIMP_ROOT}"
    "${OGL_WS_MODULES_DIR}/pugixml_x64-windows"
    "${OGL_WS_MODULES_DIR}/minizip_x64-windows"
    "${OGL_WS_MODULES_DIR}/jhasse-poly2tri_x64-windows"
    "${OGL_WS_MODULES_DIR}/zlib_x64-windows"
)
