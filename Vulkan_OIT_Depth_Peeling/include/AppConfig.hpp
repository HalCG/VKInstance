#ifndef OIT_APP_CONFIG_HPP
#define OIT_APP_CONFIG_HPP

// =============================================================================
// AppConfig — 与 OpenGL_OIT_Depth_Peeling/include/AppConfig.hpp 保持一致
// =============================================================================

#include <glm/glm.hpp>
#include <string>

namespace AppConfig {
constexpr unsigned int kInitialWidth = 800;
constexpr unsigned int kInitialHeight = 600;

constexpr const char *kResourceRoot = "resources/";
constexpr const char *kWindowTitle = "LearnOpenGL";

constexpr float kFovDegrees = 90.0f;
constexpr float kNearPlane = 0.1f;
constexpr float kFarPlane = 100.0f;
constexpr float kCameraOrbitRadius = 2.0f;
constexpr float kInitialOrbitAngle = 45.0f;
constexpr float kModelScale = 0.5f; // spot / quad 统一缩放，与 OpenGL modelMatrix 默认 scale 一致

constexpr int kMaxDepthPeelLayers = 10; // 最大剥离层数（OpenGL 亦用 10；可配合 occlusion query 提前退出）

inline const glm::vec3 &cameraPosition() {
    static const glm::vec3 v(0.0f, 0.0f, 2.0f);
    return v;
}
inline const glm::vec3 &lightPosition() {
    static const glm::vec3 v(2.0f, 2.0f, 0.0f);
    return v;
}
/** k.x=环境光, k.y=漫反射, k.z=高光 — 传入 Peel 片元 UBO */
inline const glm::vec3 &materialCoeffs() {
    static const glm::vec3 v(0.4f, 0.4f, 0.2f);
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
