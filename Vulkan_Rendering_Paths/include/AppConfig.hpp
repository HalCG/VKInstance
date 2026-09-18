#ifndef APP_CONFIG_HPP
#define APP_CONFIG_HPP

// =============================================================================
// AppConfig — 全局常量与资源路径
// =============================================================================
//
// 【资源目录结构】（相对 exe 工作目录）
//   resources/shaders/*.spv   — CMake vk_ws_compile_shaders 编译部署
//   resources/models/spot/    — spot.obj + spot.png
//
// 【灯光预设】 kLightCountPresets = {64, 128, 256, 512}
//   [ 键减少、] 键增加；影响 Forward 片元循环次数与 Tile 裁剪负载
//
// 【Forward+ Tile】
//   kTileSize=16 → 1280×720 下约 80×45=3600 个 Tile
//   kMaxLightsPerTile=64 → 单 Tile 灯数溢出时静默丢弃（Demo 简化）
//
// 【materialCoeffs】 ambient/diffuse/specular，与 forward.frag / forward_plus.frag 中 materialK 对应
//
// =============================================================================

#include <glm/glm.hpp>
#include <string>

namespace AppConfig {
constexpr unsigned int kInitialWidth = 1280;
constexpr unsigned int kInitialHeight = 720;

constexpr const char *kResourceRoot = "resources/";
constexpr const char *kWindowTitle = "Vulkan Rendering Paths Demo";

// 透视相机参数（与 OpenGL 习惯一致；buildCamera 里 projection[1][1] *= -1 适配 Vulkan NDC Y 向下）
constexpr float kFovDegrees = 45.0f;
constexpr float kNearPlane = 0.1f;
constexpr float kFarPlane = 100.0f;
constexpr float kInitialOrbitAngle = 45.0f;
constexpr float kInitialOrbitPitch = 22.0f;
constexpr float kMinOrbitRadius = 3.0f;
constexpr float kMaxOrbitRadius = 24.0f;
constexpr float kMouseOrbitFullWidthDegrees = 100.0f;
constexpr float kPanFactor = 2.0f;
constexpr float kDollyFactor = 3.0f;
constexpr float kScrollZoomSensitivity = 0.9f;

// 场景：地面 + 12 个 spot 模型实例（3×4 网格）
constexpr float kSpotScale = 1.2f;
constexpr float kSpotGridSpacing = 3.8f;
constexpr float kCameraOrbitRadius = 7.0f;
constexpr float kCameraTargetY = 0.8f;

// 点光源上限；Forward+ 每个 16×16 Tile 最多收录 kMaxLightsPerTile 盏灯
constexpr int kMaxLights = 512;
constexpr int kTileSize = 16;
constexpr int kMaxLightsPerTile = 64;
constexpr int kSpotInstanceCount = 12;

// [ / ] 键切换的四档灯光数量
constexpr int kLightCountPresets[] = {64, 128, 256, 512};
constexpr int kLightPresetCount = 4;

inline std::string resourcePath(const std::string &relative) {
    return std::string(kResourceRoot) + relative;
}

inline std::string shaderPath(const std::string &name) {
    return resourcePath("shaders/" + name);
}

// materialK: (ambient, diffuse, specular) 系数，与片元着色器中的 Blinn-Phong 一致
inline const glm::vec3 &materialCoeffs() {
    static const glm::vec3 v(0.15f, 0.75f, 0.35f);
    return v;
}
} // namespace AppConfig

#endif
