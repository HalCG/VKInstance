#ifndef RENDER_TYPES_HPP
#define RENDER_TYPES_HPP

// =============================================================================
// RenderTypes — 跨模块共享的枚举与 POD（不含 Vulkan 句柄）
// =============================================================================
//
// GpuPointLight / FrameCamera 在 C++ 与 GLSL 之间传递，布局须与 Shader 一致。
// FrameStats 字段由各 Renderer 的 render() 写入，RenderingPathsApp 读作 HUD/日志。
//
// =============================================================================

#include <glm/glm.hpp>

// 当前激活的渲染路径（键盘 1/2/3 或 F1/F2/F3）
enum class RenderPath {
    Forward = 0,    // 经典前向：几何 + 逐像素累加所有灯
    Deferred = 1,   // 延迟：G-Buffer 几何 Pass + 全屏光照 Pass
    ForwardPlus = 2 // 前向+：CPU Tile 光源裁剪 + 前向着色
};

// GPU 端点光源布局，与 shader 中 std430 PointLight 一致（std140/std430 下 vec4 自然对齐）
struct GpuPointLight {
    glm::vec4 positionRadius;  // xyz = 世界位置, w = 影响半径（片元里 smoothstep 衰减）
    glm::vec4 colorIntensity;  // rgb = 颜色, w = 强度倍率
};

// 每帧由 RenderingPathsApp::buildCamera() 构建，传给各 Renderer::render()
struct FrameCamera {
    glm::mat4 view;
    glm::mat4 projection; // 已做 Vulkan Y 翻转（projection[1][1] *= -1）
    glm::vec3 eye;          // 轨道相机眼点，写入片元 UBO 的 cameraPos
};

// PerfStats 写入的各阶段耗时（毫秒），用于窗口标题与控制台 [Perf Log]
struct FrameStats {
    float forwardPassMs = 0.0f;   // Forward 整条 RenderPass
    float geometryPassMs = 0.0f;  // Deferred Pass1 G-Buffer
    float lightingPassMs = 0.0f;  // Deferred Pass2 全屏光照
    float cullPassMs = 0.0f;      // Forward+ CPU buildForwardPlusTiles
    float shadingPassMs = 0.0f;   // Forward+ GPU 绘制 Pass
    float totalFrameMs = 0.0f;    // renderFrame 录制+提交段（CPU 计时）
    float fps = 0.0f;
};

#endif
