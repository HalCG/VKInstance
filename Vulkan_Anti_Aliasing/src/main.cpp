// =============================================================================
// Vulkan_Anti_Aliasing — 程序入口
// =============================================================================
// DemoApp 负责 Vulkan 初始化、Swapchain 同步与主循环；
// AntiAliasingDemo 注册回调，在每帧 DemoFrame 中按当前模式录制绘制命令。
//
// 按键：1=None  2=MSAA  3=FXAA  4=TAA
// =============================================================================

#include "AntiAliasingDemo.hpp"
#include "DemoApp.hpp"

#include <iostream>

int main() {
    std::cout << "===========================================" << std::endl;
    std::cout << " Vulkan Anti-Aliasing (None / MSAA / FXAA / TAA)" << std::endl;
    std::cout << "===========================================" << std::endl;

    try {
        DemoApp app("Vulkan AA Demo");
        AntiAliasingDemo demo(app);
        demo.init();

        app.setResizeCallback([&]() { demo.onResize(); });
        app.setKeyCallback([&](int key, int action) { demo.onKey(key, action); });
        app.setFrameCallback([&](DemoFrame &frame) { demo.render(frame); });
        app.run();

        demo.shutdown();
    } catch (const std::exception &e) {
        std::cerr << "[Vulkan Error] " << e.what() << std::endl;
        return -1;
    }
    return 0;
}
