#ifndef RENDERING_PATHS_APP_HPP
#define RENDERING_PATHS_APP_HPP

// =============================================================================
// RenderingPathsApp — 应用主循环、Swapchain 与三种渲染路径调度
// =============================================================================
//
// 【项目结构】
//   RenderingPathsApp   本文件：窗口、Swapchain、同步、输入、每帧调度
//   VulkanRhi           薄 RHI：RenderPass / Pipeline / Descriptor 工具函数
//   ForwardRenderer     单 Pass 前向（forward.frag）
//   DeferredRenderer    两 Pass 延迟（geometry.frag + deferred_lighting.frag）
//   ForwardPlusRenderer 单 Pass 前向+（forward_plus.frag + CPU Tile 裁剪）
//   Scene               网格上传、DrawCall（Push Constant 传 model）
//   LightManager        点光源 SSBO + Forward+ Tile 数据
//   VulkanContext       (Vulkan_Common) Instance/Device/Swapchain/队列
//
// 【一帧完整流程 — 与 OpenGL 的对应关系】
//
//   OpenGL 隐式完成的事，Vulkan 必须显式表达：
//
//   ┌─────────────────────────────────────────────────────────────────────┐
//   │ 1. vkWaitForFences(inFlightFences_[currentFrame_])                 │
//   │    → 等待「当前 CPU 帧槽」上一轮 GPU 工作结束                        │
//   │    （Fence 是 CPU↔GPU 同步；与 glFinish 类似但可 per-frame）          │
//   ├─────────────────────────────────────────────────────────────────────┤
//   │ 2. vkAcquireNextImageKHR → imageIndex                               │
//   │    → 从 Swapchain 取一张可写的后台图像                               │
//   │    → signal: imageAvailableSemaphores_[currentFrame_]              │
//   ├─────────────────────────────────────────────────────────────────────┤
//   │ 3. vkWaitForFences(imagesInFlight_[imageIndex])  （若需要）         │
//   │    → 同一张 Swapchain 图可能被 N 帧 in-flight 复用，需等旧帧完成      │
//   ├─────────────────────────────────────────────────────────────────────┤
//   │ 4. vkBeginCommandBuffer → Renderer::render() → vkEndCommandBuffer   │
//   │    → 录制绘制命令（RenderPass / Draw / Barrier 全在这里）            │
//   ├─────────────────────────────────────────────────────────────────────┤
//   │ 5. vkQueueSubmit(wait=imageAvailable, signal=renderFinished, fence) │
//   │    → GPU 在 COLOR_ATTACHMENT_OUTPUT 阶段等待 Acquire 完成           │
//   │    → renderFinishedSemaphores_[imageIndex] 按图像索引，避免复用冲突  │
//   ├─────────────────────────────────────────────────────────────────────┤
//   │ 6. vkQueuePresentKHR(wait=renderFinished)                           │
//   │    → 把 imageIndex 对应图像显示到屏幕                                │
//   └─────────────────────────────────────────────────────────────────────┘
//
// 【双缓冲索引设计 — 容易混淆的两套下标】
//
//   currentFrame_     0..1 轮转，选择 commandBuffers_[i] 和 inFlightFences_[i]
//   imageIndex        0..N-1，Swapchain 后台图像下标（Acquire 返回）
//
//   imageAvailableSemaphores_  长度 = kMaxFramesInFlight (2)
//   renderFinishedSemaphores_  长度 = Swapchain 图像数 (通常 3)
//   imagesInFlight_            长度 = Swapchain 图像数，记录每张图被哪把 Fence 占用
//
// 【Descriptor 更新规则 — 非常重要】
//
//   bindResources() 仅在 init / recreateSwapchain 后调用一次。
//   每帧只通过 mapped UBO（memcpy）更新相机、灯光数量等动态数据。
//   切勿在 GPU 仍引用 Descriptor Set 时调用 vkUpdateDescriptorSets，
//   否则 Validation 报错且会导致画面异常、长时间运行卡顿。
//
// 【三种渲染路径对比】
//
//   Forward      几何+光照同一 Pass，片元循环全部光源；灯少时最简单
//   Deferred     Pass1 写 G-Buffer，Pass2 全屏光照；灯多时片元成本与像素数相关
//   Forward+     同 Forward 但 CPU 按 Tile 裁剪光源；折中方案
//
// =============================================================================

#include "AppConfig.hpp"
#include "DeferredRenderer.hpp"
#include "ForwardPlusRenderer.hpp"
#include "ForwardRenderer.hpp"
#include "LightManager.hpp"
#include "PerfStats.hpp"
#include "RenderTypes.hpp"
#include "Scene.hpp"
#include "VtkTrackballCamera.hpp"
#include "VulkanContext.hpp"

#include <string>
#include <vector>

class RenderingPathsApp {
public:
    bool init();
    void run();
    void shutdown();

private:
    bool createSwapchainTargets();
    void destroySwapchainTargets();
    void recreateSwapchain();
    FrameCamera buildCamera() const;
    void renderFrame();
    void updateWindowTitle();
    std::string buildOverlayText() const;
    int nextLightPreset(int delta) const;
    void refreshOverlayTitle();

    static void framebufferSizeCallback(GLFWwindow *window, int width, int height);
    static void windowRefreshCallback(GLFWwindow *window);
    static void keyCallback(GLFWwindow *window, int key, int scancode, int action, int mods);
    static void mouseButtonCallback(GLFWwindow *window, int button, int action, int mods);
    static void cursorPosCallback(GLFWwindow *window, double xpos, double ypos);
    static void scrollCallback(GLFWwindow *window, double xoffset, double yoffset);

    VulkanContext ctx_{static_cast<int>(AppConfig::kInitialWidth), static_cast<int>(AppConfig::kInitialHeight),
                       AppConfig::kWindowTitle};

    VkRenderPass swapchainRenderPass_ = VK_NULL_HANDLE;
    VkImage depthImage_ = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory_ = VK_NULL_HANDLE;
    VkImageView depthView_ = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> swapchainFramebuffers_;
    std::vector<VkCommandBuffer> commandBuffers_;

    static constexpr int kMaxFramesInFlight = 2;
    std::vector<VkSemaphore> imageAvailableSemaphores_;
    std::vector<VkSemaphore> renderFinishedSemaphores_;
    std::vector<VkFence> inFlightFences_;
    std::vector<VkFence> imagesInFlight_;
    size_t currentFrame_ = 0;

    unsigned int width_ = AppConfig::kInitialWidth;
    unsigned int height_ = AppConfig::kInitialHeight;
    bool framebufferResized_ = false;

    RenderPath currentPath_ = RenderPath::Deferred;
    bool showGBufferDebug_ = false;
    bool enableHDR_ = true;
    VtkTrackballCamera camera_;
    int lightPresetIndex_ = 0;
    double lastTitleUpdate_ = 0.0;
    std::string cachedOverlayText_;

    Scene scene_;
    LightManager lights_;
    ForwardRenderer forward_;
    ForwardPlusRenderer forwardPlus_;
    DeferredRenderer deferred_;
    PerfStats perf_;

    static RenderingPathsApp *s_instance_;
};

#endif
