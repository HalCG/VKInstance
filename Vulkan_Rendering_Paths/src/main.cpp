// =============================================================================
// Vulkan_Rendering_Paths — 程序入口
// =============================================================================
//
// 【本 Demo 要回答的问题】
//   同样场景、同样点光源，Forward / Deferred / Forward+ 三种管线有何差异？
//   灯数增加时，哪条路径的片元/CPU 开销增长更快？
//
// 【建议阅读顺序（两年没碰 Vulkan 可照此复习）】
//
//   1. RenderingPathsApp.hpp     一帧流程、双缓冲、Semaphore/Fence 分工
//   2. RenderingPathsApp.cpp     renderFrame() 逐步实现对照
//   3. VulkanRhi.hpp              RenderPass / Pipeline / Descriptor 概念
//   4. shaders/mesh.vert          顶点阶段：UBO + Push Constant
//   5. ForwardRenderer.cpp        最简单的单 Pass 路径
//   6. DeferredRenderer.cpp       两 Pass + G-Buffer + Barrier
//   7. ForwardPlusRenderer.cpp    CPU Tile 裁剪
//   8. LightManager.cpp           光源 SSBO 与 Tile 数据结构
//   9. Scene.cpp                  网格上传（Staging → Device Local）
//
// 【快捷键】
//   1 / F1  → Forward      2 / F2 → Deferred      3 / F3 → Forward+
//   [ / ]   → 减少/增加灯光数量 (64/128/256/512)
//   G       → Deferred 模式下切换 G-Buffer 调试显示
//   H       → 开关 HDR Tone Mapping
//   鼠标    → 左旋转 / 中平移 / 右缩放 / 滚轮缩放
//
// =============================================================================

#include "RenderingPathsApp.hpp"

#include <iostream>

int main() {
    RenderingPathsApp app;
    try {
        if (!app.init()) {
            std::cerr << "[Vulkan_Rendering_Paths] Failed to initialize." << std::endl;
            return -1;
        }
        app.run();
        app.shutdown();
    } catch (const std::exception &e) {
        std::cerr << "[Vulkan Error] " << e.what() << std::endl;
        return -1;
    }
    return 0;
}
