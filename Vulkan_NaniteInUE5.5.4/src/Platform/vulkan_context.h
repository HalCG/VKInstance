#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#include <vector>
#include <iostream>
#include <stdexcept>
#include <Windows.h>

// ==============================================================================
// 交换链支持细节结构体 (包含表面能力、颜色格式列表和呈现模式)
// ==============================================================================
struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;         // 表面能力 (如最大/最小图像数量、图像分辨率范围)
    std::vector<VkSurfaceFormatKHR> formats;      // 支持的颜色像素格式 (如 RGBA8 / BGRA8)
    std::vector<VkPresentModeKHR> presentModes;   // 支持的屏幕呈现模式 (如 Mailbox, FIFO, Immediate)
};

// ==============================================================================
// Vulkan 基础上下文封装类 (负责 Instance、Device、Swapchain、CommandPool 和同步原语)
// ==============================================================================
class VulkanContext {
public:
    VulkanContext();
    ~VulkanContext();

    // 初始化 Vulkan 上下文 (创建窗口 Surface, 逻辑设备, 交换链等)
    void Init(HWND hwnd, HINSTANCE hInstance, uint32_t width, uint32_t height);
    // 销毁并释放所有 Vulkan 硬件资源
    void Cleanup();

    // 辅助函数：依据显卡内存属性类型查找适合的内存索引 (Device Local 或 Host Visible)
    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

    // 一次性 Command Buffer 辅助函数 (用于数据上传或图像 Layout 转换)
    VkCommandBuffer BeginSingleTimeCommands();
    void EndSingleTimeCommands(VkCommandBuffer commandBuffer);

    // 帧渲染同步控制 (等待 Fence、获取 Swapchain 图像、重置 Command Buffer)
    void BeginFrame();
    void EndFrame();

public:
    HWND mHwnd = nullptr;                 // Win32 窗口句柄
    HINSTANCE mHinstance = nullptr;       // 应用程序实例句柄
    uint32_t mWidth = 1280;               // 视口宽度
    uint32_t mHeight = 720;               // 视口高度

    VkInstance mInstance = VK_NULL_HANDLE;           // Vulkan 实例句柄
    VkSurfaceKHR mSurface = VK_NULL_HANDLE;         // 窗口渲染表面句柄 (Win32 Surface)
    VkPhysicalDevice mPhysicalDevice = VK_NULL_HANDLE; // 物理显卡设备句柄
    VkDevice mDevice = VK_NULL_HANDLE;               // 逻辑设备句柄

    // 队列族索引 (图形、计算、呈现队列)
    uint32_t mGraphicsQueueFamilyIndex = UINT32_MAX;
    uint32_t mComputeQueueFamilyIndex = UINT32_MAX;
    uint32_t mPresentQueueFamilyIndex = UINT32_MAX;

    // 队列句柄
    VkQueue mGraphicsQueue = VK_NULL_HANDLE;
    VkQueue mComputeQueue = VK_NULL_HANDLE;
    VkQueue mPresentQueue = VK_NULL_HANDLE;

    // 交换链相关
    VkSwapchainKHR mSwapchain = VK_NULL_HANDLE;
    VkFormat mSwapchainImageFormat;
    VkExtent2D mSwapchainExtent;
    std::vector<VkImage> mSwapchainImages;
    std::vector<VkImageView> mSwapchainImageViews;
    std::vector<VkFramebuffer> mSwapchainFramebuffers;

    // 指令池与主指令缓冲
    VkCommandPool mCommandPool = VK_NULL_HANDLE;
    VkCommandBuffer mCommandBuffer = VK_NULL_HANDLE;

    // CPU-GPU 帧同步原语
    VkSemaphore mImageAvailableSemaphore = VK_NULL_HANDLE; // 交换链图像可用信号量
    VkSemaphore mRenderFinishedSemaphore = VK_NULL_HANDLE; // 渲染完成信号量
    VkFence mInFlightFence = VK_NULL_HANDLE;               // CPU-GPU 并发栅栏 (Prevent Multi-Frame Race)

    uint32_t mCurrentImageIndex = 0;                        // 当前获取到的交换链图像索引

private:
    void CreateInstance();
    void CreateSurface();
    void SelectPhysicalDevice();
    void CreateLogicalDevice();
    void CreateSwapchain();
    void CreateImageViews();
    void CreateCommandPool();
    void CreateCommandBuffers();
    void CreateSyncObjects();

    SwapChainSupportDetails QuerySwapChainSupport(VkPhysicalDevice device);
    VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
    VkPresentModeKHR ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
    VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);
};
