#ifndef VTK_TRACKBALL_CAMERA_HPP
#define VTK_TRACKBALL_CAMERA_HPP

#include "AppConfig.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

enum class CameraDragMode { None, Rotate, Pan, Dolly };

struct VtkTrackballCamera {
    glm::vec3 target = glm::vec3(0.0f, AppConfig::kCameraTargetY, 0.0f);
    float yaw = AppConfig::kInitialOrbitAngle;
    float pitch = AppConfig::kInitialOrbitPitch;
    float radius = AppConfig::kCameraOrbitRadius;

    CameraDragMode dragMode = CameraDragMode::None;
    bool skipDragDelta = false;
    double lastCursorX = 0.0;
    double lastCursorY = 0.0;

    glm::vec3 eye() const {
        const float yawRad = glm::radians(yaw);
        const float pitchRad = glm::radians(pitch);
        return target + glm::vec3(radius * glm::cos(pitchRad) * glm::sin(yawRad), radius * glm::sin(pitchRad),
                                  radius * glm::cos(pitchRad) * glm::cos(yawRad));
    }

    glm::mat4 viewMatrix() const { return glm::lookAt(eye(), target, glm::vec3(0.0f, 1.0f, 0.0f)); }

    void beginDrag(CameraDragMode mode, double x, double y) {
        dragMode = mode;
        skipDragDelta = true;
        lastCursorX = x;
        lastCursorY = y;
    }

    void endDrag() {
        dragMode = CameraDragMode::None;
        skipDragDelta = false;
    }

    bool isDragging() const { return dragMode != CameraDragMode::None; }

    bool applyCursorDelta(double x, double y, unsigned width, unsigned height) {
        if (dragMode == CameraDragMode::None) {
            return false;
        }
        if (skipDragDelta) {
            lastCursorX = x;
            lastCursorY = y;
            skipDragDelta = false;
            return false;
        }

        const double deltaX = x - lastCursorX;
        const double deltaY = y - lastCursorY;
        lastCursorX = x;
        lastCursorY = y;

        const float w = static_cast<float>(width > 0 ? width : AppConfig::kInitialWidth);
        const float h = static_cast<float>(height > 0 ? height : AppConfig::kInitialHeight);

        switch (dragMode) {
        case CameraDragMode::Rotate: {
            const float sensX = AppConfig::kMouseOrbitFullWidthDegrees / w;
            const float sensY = AppConfig::kMouseOrbitFullWidthDegrees / h;
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
            const float panScale = radius * AppConfig::kPanFactor / h;
            target -= right * static_cast<float>(deltaX) * panScale;
            target += up * static_cast<float>(deltaY) * panScale;
            break;
        }
        case CameraDragMode::Dolly: {
            const float dollyScale = AppConfig::kDollyFactor * radius / h;
            radius -= static_cast<float>(deltaY) * dollyScale;
            radius = glm::clamp(radius, AppConfig::kMinOrbitRadius, AppConfig::kMaxOrbitRadius);
            break;
        }
        default:
            break;
        }
        return true;
    }

    void applyScroll(double yoffset) {
        radius -= static_cast<float>(yoffset) * AppConfig::kScrollZoomSensitivity;
        radius = glm::clamp(radius, AppConfig::kMinOrbitRadius, AppConfig::kMaxOrbitRadius);
    }
};

#endif
