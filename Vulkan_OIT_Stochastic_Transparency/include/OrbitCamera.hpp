#ifndef STOCHASTIC_ORBIT_CAMERA_HPP
#define STOCHASTIC_ORBIT_CAMERA_HPP

/**
 * @file OrbitCamera.hpp
 * @brief 轨道相机 — 与 OpenGL StochasticTransparencyApp 一致
 *
 * 相机在 XZ 平面绕原点旋转，lookAt 目标恒为 (0,0,0)。
 * OpenGL 版公式：
 *   eye = 2.0 * (sin(angle), 0, cos(angle))
 * 此处 orbitRadius = kCameraOrbitRadius = 2.0，等价。
 *
 * 交互：
 *   - 左键拖拽：水平旋转
 *   - 滚轮：缩放 orbitRadius
 *   - 左右方向键：每帧 ±1°（由 StochasticDemo::processInput 处理）
 */

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

    /** 当前相机世界坐标（XZ 平面圆周运动） */
    glm::vec3 eye() const {
        const float rad = glm::radians(orbitAngleDeg);
        return AppConfig::kCameraOrbitRadius *
               glm::vec3(glm::sin(rad), 0.0f, glm::cos(rad));
    }

    glm::mat4 view() const {
        return glm::lookAt(eye(), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    }

    /** Vulkan Y 轴翻转（clip space Y 向下） */
    glm::mat4 projection() const {
        glm::mat4 proj = glm::perspective(glm::radians(fovDeg), aspect, nearPlane, farPlane);
        proj[1][1] *= -1.0f;
        return proj;
    }

    void setAspectFromViewport(unsigned int width, unsigned int height) {
        aspect = static_cast<float>(width) / static_cast<float>(height > 0 ? height : 1);
    }

    void onMouseButton(int button, int action, double /*x*/, double /*y*/) {
        if (action == GLFW_PRESS && button == GLFW_MOUSE_BUTTON_LEFT) {
            dragging_ = true;
            skipDragDelta_ = true;
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
