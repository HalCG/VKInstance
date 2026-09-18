#ifndef STOCHASTIC_APP_CONFIG_HPP
#define STOCHASTIC_APP_CONFIG_HPP

/**
 * @file AppConfig.hpp
 * @brief 应用级常量：窗口、资源路径、相机与场景默认值
 *
 * 数值与 OpenGL_OIT_Stochastic_Transparency/include/AppConfig.hpp 保持一致，
 * 便于两个 Demo 对照观察 Stochastic Transparency 效果。
 */

#include <glm/glm.hpp>
#include <string>

namespace AppConfig {
/** 初始窗口尺寸（与 OpenGL 版相同） */
constexpr unsigned int kInitialWidth = 800;
constexpr unsigned int kInitialHeight = 600;

/** 运行时资源根目录（由 CMake POST_BUILD 复制到 exe 旁） */
constexpr const char *kResourceRoot = "resources/";
constexpr const char *kWindowTitle = "Vulkan OIT - Stochastic Transparency";

/** 透视投影参数：OpenGL 版 kFovDegrees = 90 */
constexpr float kFovDegrees = 90.0f;
constexpr float kNearPlane = 0.1f;
constexpr float kFarPlane = 100.0f;

/** 轨道相机：半径 2.0，初始角度 45°（与 OpenGL viewRotate_ 初值一致） */
constexpr float kCameraOrbitRadius = 2.0f;
constexpr float kInitialOrbitAngle = 45.0f;

/** spot / quad 模型统一缩放（OpenGL modelMatrix 默认 scale = 0.5） */
constexpr float kModelScale = 0.5f;

inline const glm::vec3 &cameraPosition() {
    static const glm::vec3 v(0.0f, 0.0f, 2.0f);
    return v;
}

/** 清屏背景色（OpenGL glClearColor(0.2, 0.3, 0.3, 1)） */
inline const glm::vec3 &backgroundColor() {
    static const glm::vec3 v(0.2f, 0.3f, 0.3f);
    return v;
}

/** 拼接 resources/ 前缀得到完整相对路径 */
inline std::string resourcePath(const std::string &relative) {
    return std::string(kResourceRoot) + relative;
}
} // namespace AppConfig

#endif
