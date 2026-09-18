#include "vulkan_context.h"
#include "scene.h"
#include "utils.h"

#if _DEBUG
#pragma comment(linker, "/subsystem:\"console\" /entry:\"WinMainCRTStartup\"")
#endif

// ==============================================================================
// Win32 窗口消息回调函数 (处理窗口关闭、键盘快捷键与鼠标 Trackball 拖拽交互)
// ==============================================================================
LRESULT CALLBACK VulkanWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CLOSE:
        // 窗口关闭：发送退出消息中断主循环
        PostQuitMessage(0);
        return 0;
    case WM_KEYUP:
        // 方向键控制镜头推拉
        OnKeyUp(wParam);
        break;
    case WM_LBUTTONDOWN:
        // 鼠标左键按下：进入 Trackball 旋转模式
        OnMousePress(0, 1, (int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam));
        break;
    case WM_LBUTTONUP:
        OnMousePress(0, 0, (int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam));
        break;
    case WM_MBUTTONDOWN:
        // 鼠标中键按下：进入平移模式
        OnMousePress(1, 1, (int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam));
        break;
    case WM_MBUTTONUP:
        OnMousePress(1, 0, (int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam));
        break;
    case WM_RBUTTONDOWN:
        // 鼠标右键按下：进入缩放 Dolly 模式
        OnMousePress(2, 1, (int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam));
        break;
    case WM_RBUTTONUP:
        OnMousePress(2, 0, (int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam));
        break;
    case WM_MOUSEMOVE:
        // 鼠标移动：应用拖拽增量更新视角
        OnMouseMove((int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam));
        break;
    case WM_MOUSEWHEEL:
        // 鼠标滚轮：缩放视角
        OnMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam));
        break;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// ==============================================================================
// Windows 应用程序入口主函数
// ==============================================================================
INT WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd) {
    // 1. 注册 Win32 窗口类
    WNDCLASSEXA wc{};
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = VulkanWindowProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = "NaniteVulkanWindowClass";

    if (!RegisterClassExA(&wc)) {
        printf("注册 Win32 窗口类失败\n");
        return -1;
    }

    // 2. 调整并创建 1280x720 视口窗口
    RECT rect = { 0, 0, 1280, 720 };
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

    HWND hwnd = CreateWindowExA(
        0,
        "NaniteVulkanWindowClass",
        "Nanite UE5.5.4 Vulkan GPU-Driven Renderer (0016 目录)",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        100, 100,
        rect.right - rect.left,
        rect.bottom - rect.top,
        NULL, NULL, hInstance, NULL
    );

    if (!hwnd) {
        printf("创建 Win32 窗口失败\n");
        return -1;
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    // 3. 初始化 Vulkan 上下文与 Nanite 渲染场景
    VulkanContext ctx;
    try {
        ctx.Init(hwnd, hInstance, 1280, 720);
        InitScene(&ctx, 1280, 720);
    } catch (const std::exception& e) {
        printf("Vulkan 初始化异常: %s\n", e.what());
        return -1;
    }

    printf("=======================================================\n");
    printf("Nanite Vulkan GPU-Driven 渲染管线初始化成功并开启主循环!\n");
    printf("=======================================================\n");

    // 4. 主消息与渲染循环
    MSG msg{};
    bool bRunning = true;
    while (bRunning) {
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                bRunning = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }

        if (!bRunning) break;

        float frameTime = GetFrameTime();
        try {
            RenderOneFrame(&ctx, frameTime); // 渲染一帧
        } catch (const std::exception& e) {
            printf("渲染帧异常: %s\n", e.what());
            break;
        }
    }

    // 5. 退出清理
    CleanupScene();
    ctx.Cleanup();

    return 0;
}
