/**
 * @file main.cpp
 * @brief 程序入口 — 初始化 DemoApp 帧循环，注册 StochasticDemo 回调
 *
 * DemoApp 负责 Swapchain / 同步 / 窗口事件；
 * StochasticDemo 负责 MSAA 随机透明度渲染与场景绘制。
 */

#include "StochasticDemo.hpp"
#include "AppConfig.hpp"
#include "DemoApp.hpp"

#include <iostream>

int main() {
    std::cout << "===========================================" << std::endl;
    std::cout << " Vulkan Stochastic Transparency Demo" << std::endl;
    std::cout << " Scene matched to OpenGL OIT Stochastic Transparency" << std::endl;
    std::cout << " Left/Right: orbit | Mouse drag: orbit | Scroll: zoom | ESC: quit" << std::endl;
    std::cout << "===========================================" << std::endl;

    try {
        DemoApp app(AppConfig::kWindowTitle, AppConfig::kInitialWidth, AppConfig::kInitialHeight);
        app.setBuiltinCameraEnabled(false); // 使用 OrbitCamera，禁用 DemoApp 内置相机

        StochasticDemo demo(app);
        demo.init();
        app.setResizeCallback([&]() { demo.onResize(); });
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
