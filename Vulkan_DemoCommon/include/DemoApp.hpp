#ifndef DEMO_APP_HPP
#define DEMO_APP_HPP

// =============================================================================
// DemoApp — 可复用的 Vulkan 小程序框架（AA / OIT 等 Demo 共用）
// =============================================================================
// 封装：Swapchain RenderPass、深度图、Framebuffer、双缓冲同步、轨道相机输入。
// 具体 Demo 通过 setFrameCallback 在每帧收到 DemoFrame（cmd / fb / 相机矩阵等）。
//
// 同步模型与 RenderingPathsApp 相同：
//   Fence 按 frameIndex_ 轮转；renderFinished 按 Swapchain imageIndex 索引。
// =============================================================================

#include "SimpleCamera.hpp"
#include "VulkanContext.hpp"

#include <functional>
#include <string>
#include <vector>

// 每帧传给 Demo 回调的上下文
struct DemoFrame {
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    uint32_t imageIndex = 0;
    VkExtent2D extent{};
    glm::mat4 view{};
    glm::mat4 proj{}; // 已 Vulkan Y 翻转
    float time = 0.0f;
};

class DemoApp {
public:
    using FrameCallback = std::function<void(DemoFrame &)>;
    using KeyCallback = std::function<void(int key, int action)>;
    using MouseButtonCallback = std::function<void(int button, int action, double x, double y)>;
    using CursorCallback = std::function<void(double x, double y, int width, int height)>;
    using ScrollCallback = std::function<void(double yoffset)>;

    DemoApp(const std::string &title, int width = 1280, int height = 720);
    ~DemoApp();

    void setFrameCallback(FrameCallback cb) { frameCb_ = std::move(cb); }
    void setKeyCallback(KeyCallback cb) { keyCb_ = std::move(cb); }
    void setResizeCallback(std::function<void()> cb) { resizeCb_ = std::move(cb); }
    void setMouseButtonCallback(MouseButtonCallback cb) { mouseButtonCb_ = std::move(cb); }
    void setCursorCallback(CursorCallback cb) { cursorCb_ = std::move(cb); }
    void setScrollCallback(ScrollCallback cb) { scrollCb_ = std::move(cb); }
    void setBuiltinCameraEnabled(bool enabled) { builtinCameraEnabled_ = enabled; }
    void run();

    VulkanContext &ctx() { return ctx_; }
    VkRenderPass swapRenderPass() const { return swapRenderPass_; }
    VkImage depthImage() const { return depthImage_; }
    VkImageView depthView() const { return depthView_; }
    VkExtent2D extent() const { return ctx_.swapChainExtent(); }

private:
    void createTargets();
    void destroyTargets();
    void recreateSwapchain();

    static void framebufferSizeCallback(GLFWwindow *window, int width, int height);
    static void windowRefreshCallback(GLFWwindow *window);
    static void keyCallback(GLFWwindow *window, int key, int scancode, int action, int mods);
    static void mouseButtonCallback(GLFWwindow *window, int button, int action, int mods);
    static void cursorPosCallback(GLFWwindow *window, double xpos, double ypos);
    static void scrollCallback(GLFWwindow *window, double xoffset, double yoffset);

    VulkanContext ctx_;
    VkRenderPass swapRenderPass_ = VK_NULL_HANDLE;
    VkImage depthImage_ = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory_ = VK_NULL_HANDLE;
    VkImageView depthView_ = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers_;
    std::vector<VkCommandBuffer> commandBuffers_;

    std::vector<VkSemaphore> imageAvailable_;
    std::vector<VkSemaphore> renderFinished_;
    std::vector<VkFence> inFlightFences_;
    std::vector<VkFence> imagesInFlight_;
    size_t frameIndex_ = 0;
    static constexpr int kMaxFrames = 2;

    bool resized_ = false;
    SimpleCamera camera_;
    FrameCallback frameCb_;
    KeyCallback keyCb_;
    std::function<void()> resizeCb_;
    MouseButtonCallback mouseButtonCb_;
    CursorCallback cursorCb_;
    ScrollCallback scrollCb_;
    bool builtinCameraEnabled_ = true;
    static DemoApp *instance_;
};

#endif
