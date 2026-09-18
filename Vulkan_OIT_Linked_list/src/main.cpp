// =============================================================================
// Vulkan_OIT_Linked_list — 入口
// =============================================================================
//
// 使用 DemoApp 框架（Swapchain / 同步 / 深度附件由 DemoApp 管理）；
// 关闭内置相机，由 LinkedListDemo + OrbitCamera 接管输入。
//
// 操作：鼠标左键拖拽旋转 | 滚轮缩放 | 左右方向键微调 | ESC 退出
//
// 可执行文件路径（Debug）：
//   I:\opengl\11_VkInstance\build\Vulkan_OIT_Linked_list\Debug\Vulkan_OIT_Linked_list.exe
// 须从 exe 所在目录启动（同目录含 resources/ 与 assimp DLL）。
//
// =============================================================================

#include "LinkedListDemo.hpp"
#include "AppConfig.hpp"
#include "DemoApp.hpp"

#include <iostream>

int main() {
    std::cout << "===========================================" << std::endl;
    std::cout << " Vulkan OIT Linked-List (OpenGL scene)" << std::endl;
    std::cout << " Mouse: left-drag orbit, scroll zoom | Left/Right: orbit | ESC: quit" << std::endl;
    std::cout << "===========================================" << std::endl;

    try {
        DemoApp app(AppConfig::kWindowTitle, AppConfig::kInitialWidth, AppConfig::kInitialHeight);
        app.setBuiltinCameraEnabled(false);
        LinkedListDemo demo(app);
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
