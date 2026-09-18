#ifndef OIT_APP_CONFIG_HPP
#define OIT_APP_CONFIG_HPP

// =============================================================================
// AppConfig — 窗口、相机、OIT 与场景常量
// =============================================================================
//
// 数值与 OpenGL_OIT_Linked_list/include/AppConfig.hpp 保持一致，
// 便于两个工程对照调试同一套场景参数。
//
// =============================================================================

#include <glm/glm.hpp>
#include <string>

namespace AppConfig {

// --- 窗口 ---
constexpr unsigned int kInitialWidth = 800;
constexpr unsigned int kInitialHeight = 600;
constexpr const char *kResourceRoot = "resources/";
constexpr const char *kWindowTitle = "LearnOpenGL";

// --- 透视相机 ---
constexpr float kFovDegrees = 90.0f;
constexpr float kNearPlane = 0.1f;
constexpr float kFarPlane = 100.0f;
constexpr float kCameraOrbitRadius = 2.0f;
constexpr float kInitialOrbitAngle = 227.0f; // 与 OpenGL 初始视角一致
constexpr float kModelScale = 0.5f;

// --- OIT 链表 ---
// 每像素最多收集的透明片元数；节点池大小 = width * height * kMaxFragmentsPerPixel
constexpr int kMaxFragmentsPerPixel = 20;

inline const glm::vec3 &cameraPosition() {
    static const glm::vec3 v(0.0f, 0.0f, 2.0f);
    return v;
}
inline const glm::vec3 &lightPosition() {
    static const glm::vec3 v(2.0f, 2.0f, 0.0f);
    return v;
}
inline const glm::vec3 &materialCoeffs() {
    static const glm::vec3 v(0.4f, 0.4f, 0.2f); // ambient, diffuse, specular
    return v;
}
inline const glm::vec3 &backgroundColor() {
    static const glm::vec3 v(0.2f, 0.3f, 0.3f);
    return v;
}

inline std::string resourcePath(const std::string &relative) {
    return std::string(kResourceRoot) + relative;
}

} // namespace AppConfig

#endif
