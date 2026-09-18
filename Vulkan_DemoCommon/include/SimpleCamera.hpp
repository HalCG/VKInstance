#ifndef SIMPLE_CAMERA_HPP
#define SIMPLE_CAMERA_HPP

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

enum class CameraDragMode { None, Rotate, Pan, Dolly };

struct SimpleCamera {
    glm::vec3 target{0.0f, 0.8f, 0.0f};
    float yaw = 45.0f;
    float pitch = 22.0f;
    float radius = 7.0f;

    CameraDragMode dragMode = CameraDragMode::None;
    bool skipDragDelta = false;
    double lastCursorX = 0.0;
    double lastCursorY = 0.0;

    glm::vec3 eye() const {
        const float yawRad = glm::radians(yaw);
        const float pitchRad = glm::radians(pitch);
        return target + glm::vec3(radius * glm::cos(pitchRad) * glm::sin(yawRad),
                                  radius * glm::sin(pitchRad),
                                  radius * glm::cos(pitchRad) * glm::cos(yawRad));
    }

    glm::mat4 view() const {
        return glm::lookAt(eye(), target, glm::vec3(0.0f, 1.0f, 0.0f));
    }

    void onMouseButton(int button, int action, double x, double y) {
        if (action == GLFW_PRESS) {
            if (button == GLFW_MOUSE_BUTTON_LEFT) {
                dragMode = CameraDragMode::Rotate;
            } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
                dragMode = CameraDragMode::Pan;
            } else if (button == GLFW_MOUSE_BUTTON_MIDDLE) {
                dragMode = CameraDragMode::Dolly;
            }
            skipDragDelta = true;
            lastCursorX = x;
            lastCursorY = y;
        } else if (action == GLFW_RELEASE) {
            dragMode = CameraDragMode::None;
            skipDragDelta = false;
        }
    }

    void onCursorPos(double x, double y, int width = 1280, int height = 720) {
        if (dragMode == CameraDragMode::None) {
            return;
        }
        if (skipDragDelta) {
            lastCursorX = x;
            lastCursorY = y;
            skipDragDelta = false;
            return;
        }

        const double deltaX = x - lastCursorX;
        const double deltaY = y - lastCursorY;
        lastCursorX = x;
        lastCursorY = y;

        const float w = static_cast<float>(width > 0 ? width : 1280);
        const float h = static_cast<float>(height > 0 ? height : 720);

        switch (dragMode) {
        case CameraDragMode::Rotate: {
            const float sensX = 100.0f / w;
            const float sensY = 100.0f / h;
            yaw -= static_cast<float>(deltaX) * sensX;
            pitch -= static_cast<float>(-deltaY) * sensY;
            pitch = glm::clamp(pitch, -85.0f, 85.0f);
            break;
        }
        case CameraDragMode::Pan: {
            const glm::vec3 e = eye();
            const glm::vec3 forward = glm::normalize(target - e);
            const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
            const glm::vec3 right = glm::normalize(glm::cross(forward, worldUp));
            const glm::vec3 up = glm::cross(right, forward);
            const float panScale = radius * 2.0f / h;
            target -= right * static_cast<float>(deltaX) * panScale;
            target += up * static_cast<float>(deltaY) * panScale;
            break;
        }
        case CameraDragMode::Dolly: {
            const float dollyScale = 3.0f * radius / h;
            radius -= static_cast<float>(deltaY) * dollyScale;
            radius = glm::clamp(radius, 2.0f, 30.0f);
            break;
        }
        default:
            break;
        }
    }

    void onScroll(double yoffset) {
        radius -= static_cast<float>(yoffset) * 0.9f;
        radius = glm::clamp(radius, 2.0f, 30.0f);
    }

    void tick(float /*dt*/) {}

    static glm::mat4 proj(float aspect) {
        glm::mat4 p = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);
        p[1][1] *= -1.0f;
        return p;
    }
};

#endif
