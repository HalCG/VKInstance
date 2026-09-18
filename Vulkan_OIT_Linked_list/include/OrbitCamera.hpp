#ifndef OIT_ORBIT_CAMERA_HPP
#define OIT_ORBIT_CAMERA_HPP

// =============================================================================
// OrbitCamera — 绕原点 Y 轴轨道相机
// =============================================================================
//
// 与 OpenGL / Vulkan_OIT_Depth_Peeling 版 OrbitCamera 一致：
//   - 左键拖拽：水平旋转 orbitAngleDeg
//   - 滚轮：缩放 orbitRadius
//   - projection() 对 glm::perspective 做 Y 翻转（proj[1][1] *= -1），适配 Vulkan NDC
//
// 本 Demo 不自动旋转；角度仅由用户输入改变。
//
// =============================================================================

#include "AppConfig.hpp"

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class OrbitCamera {
public:
    float orbitAngleDeg = AppConfig::kInitialOrbitAngle;
    float orbitRadius = AppConfig::kCameraOrbitRadius;
    float fovDeg = AppConfig::kFovDegrees;
    float aspect = static_cast<float>(AppConfig::kInitialWidth) / static_cast<float>(AppConfig::kInitialHeight);
    float nearPlane = AppConfig::kNearPlane;
    float farPlane = AppConfig::kFarPlane;

    glm::vec3 eye() const {
        const float rad = glm::radians(orbitAngleDeg);
        return glm::vec3(orbitRadius * glm::sin(rad), 0.0f, orbitRadius * glm::cos(rad));
    }

    glm::mat4 view() const {
        return glm::lookAt(eye(), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    }

    glm::mat4 projection() const {
        glm::mat4 proj = glm::perspective(glm::radians(fovDeg), aspect, nearPlane, farPlane);
        proj[1][1] *= -1.0f; // Vulkan：NDC Y 向下
        return proj;
    }

    void setAspectFromViewport(unsigned int width, unsigned int height) {
        aspect = static_cast<float>(width) / static_cast<float>(height > 0 ? height : 1);
    }

    void onMouseButton(int button, int action, double /*x*/, double /*y*/) {
        if (action == GLFW_PRESS && button == GLFW_MOUSE_BUTTON_LEFT) {
            dragging_ = true;
            skipDragDelta_ = true; // 按下首帧不计 delta，避免跳变
        } else if (action == GLFW_RELEASE && button == GLFW_MOUSE_BUTTON_LEFT) {
            dragging_ = false;
            skipDragDelta_ = false;
        }
    }

    void onCursorPos(double x, double y, int width, int height) {
        if (!dragging_) {
            return;
        }
        if (skipDragDelta_) {
            lastX_ = x;
            lastY_ = y;
            skipDragDelta_ = false;
            return;
        }
        const double deltaX = x - lastX_;
        lastX_ = x;
        lastY_ = y;
        const float w = static_cast<float>(width > 0 ? width : 1);
        orbitAngleDeg -= static_cast<float>(deltaX) * (100.0f / w);
    }

    void onScroll(double yoffset) {
        orbitRadius -= static_cast<float>(yoffset) * 0.15f;
        orbitRadius = glm::clamp(orbitRadius, 1.0f, 8.0f);
    }

private:
    bool dragging_ = false;
    bool skipDragDelta_ = false;
    double lastX_ = 0.0;
    double lastY_ = 0.0;
};

#endif
