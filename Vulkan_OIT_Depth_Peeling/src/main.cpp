// =============================================================================
// Vulkan_OIT_Depth_Peeling — 入口
// =============================================================================
//
// 使用 DemoApp 框架（Swapchain / 同步 / 深度附件由 DemoApp 管理）；
// 关闭内置相机，由 DepthPeelingDemo + OrbitCamera 接管输入。
//
// 操作：鼠标拖拽旋转 | 滚轮缩放 | 左右方向键旋转 | ESC 退出
//
// =============================================================================

#include "DepthPeelingDemo.hpp"
#include "AppConfig.hpp"
#include "DemoApp.hpp"

#include <iostream>

int main() {
    std::cout << "===========================================" << std::endl;
    std::cout << " Vulkan OIT Depth Peeling (OpenGL scene)" << std::endl;
    std::cout << " Mouse: drag orbit, scroll zoom | Left/Right: orbit | ESC: quit" << std::endl;
    std::cout << "===========================================" << std::endl;

    try {
        DemoApp app(AppConfig::kWindowTitle, AppConfig::kInitialWidth, AppConfig::kInitialHeight);
        app.setBuiltinCameraEnabled(false);
        DepthPeelingDemo demo(app);
        demo.init();
        app.setResizeCallback([&]() { demo.onResize(); });
        app.setKeyCallback([&](int key, int action) { demo.onKey(key, action); });
        app.setMouseButtonCallback([&](int button, int action, double x, double y) {
            demo.onMouseButton(button, action, x, y);
        });
        app.setCursorCallback([&](double x, double y, int width, int height) {
            demo.onCursorPos(x, y, width, height);
        });
        app.setScrollCallback([&](double yoffset) { demo.onScroll(yoffset); });
        app.setFrameCallback([&](DemoFrame &frame) { demo.render(frame); });
        app.run();
        demo.shutdown();
    } catch (const std::exception &e) {
        std::cerr << "[Vulkan Error] " << e.what() << std::endl;
        return -1;
    }
    return 0;
}
