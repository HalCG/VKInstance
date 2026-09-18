#include "vulkan_context.h"
#include <algorithm>
#include <set>
#include <cassert>

// 辅助宏：检查 Vulkan API 返回值，若不成功则打印行号并抛出异常
#define VK_CHECK(result) \
    if (result != VK_SUCCESS) { \
        printf("Vulkan 错误代码 %d (行号: %d)\n", result, __LINE__); \
        throw std::runtime_error("Vulkan API 执行失败"); \
    }

VulkanContext::VulkanContext() {}
VulkanContext::~VulkanContext() { Cleanup(); }

// ==============================================================================
// 初始化 Vulkan 完整的驱动与硬件渲染上下文环境
// ==============================================================================
void VulkanContext::Init(HWND hwnd, HINSTANCE hInstance, uint32_t width, uint32_t height) {
    mHwnd = hwnd;
    mHinstance = hInstance;
    mWidth = width;
    mHeight = height;

    CreateInstance();         // 1. 创建 Vulkan 驱动实例
    CreateSurface();          // 2. 绑定 Win32 窗口渲染表面 (Win32 Surface)
    SelectPhysicalDevice();   // 3. 自动筛选并选择支持 Compute/Graphics 的独立显卡
    CreateLogicalDevice();    // 4. 创建逻辑设备并提取图形/计算队列
    CreateSwapchain();        // 5. 创建交换链 (Swapchain)
    CreateImageViews();       // 6. 创建交换链图像视图
    CreateCommandPool();      // 7. 创建指令池
    CreateCommandBuffers();   // 8. 分配主渲染 Command Buffer
    CreateSyncObjects();      // 9. 创建同步原语 (Fence / Semaphore)
}

// ==============================================================================
// 1. 创建 Vulkan 驱动实例 (Instance)
// ==============================================================================
void VulkanContext::CreateInstance() {
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Nanite Vulkan GPU-Driven Renderer";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "Nanite Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_2; // 使用 Vulkan 1.2 规范

    // 启用 Win32 窗口渲染所需的实例扩展
    std::vector<const char*> extensions = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME
    };

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    VK_CHECK(vkCreateInstance(&createInfo, nullptr, &mInstance));
}

// ==============================================================================
// 2. 创建 Win32 窗口 Surface 表面
// ==============================================================================
void VulkanContext::CreateSurface() {
    VkWin32SurfaceCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    createInfo.hwnd = mHwnd;
    createInfo.hinstance = mHinstance;

    VK_CHECK(vkCreateWin32SurfaceKHR(mInstance, &createInfo, nullptr, &mSurface));
}

// ==============================================================================
// 3. 选择合适的物理显卡设备 (Physical Device)
// ==============================================================================
void VulkanContext::SelectPhysicalDevice() {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(mInstance, &deviceCount, nullptr);
    if (deviceCount == 0) {
        throw std::runtime_error("找不到支持 Vulkan 的 GPU 显卡设备!");
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(mInstance, &deviceCount, devices.data());

    for (const auto& device : devices) {
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

        uint32_t graphicsIdx = UINT32_MAX;
        uint32_t computeIdx = UINT32_MAX;
        uint32_t presentIdx = UINT32_MAX;

        for (uint32_t i = 0; i < queueFamilyCount; i++) {
            if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                graphicsIdx = i;
            }
            if (queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
                computeIdx = i;
            }

            VkBool32 presentSupport = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, mSurface, &presentSupport);
            if (presentSupport) {
                presentIdx = i;
            }
        }

        // 找到同时支持图形、计算与呈现的物理设备
        if (graphicsIdx != UINT32_MAX && computeIdx != UINT32_MAX && presentIdx != UINT32_MAX) {
            mPhysicalDevice = device;
            mGraphicsQueueFamilyIndex = graphicsIdx;
            mComputeQueueFamilyIndex = computeIdx;
            mPresentQueueFamilyIndex = presentIdx;
            break;
        }
    }

    if (mPhysicalDevice == VK_NULL_HANDLE) {
        throw std::runtime_error("无法找到满足 Nanite 要求的硬件 GPU!");
    }

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(mPhysicalDevice, &props);
    printf("成功选中 GPU 设备: [%s]\n", props.deviceName);
}

// ==============================================================================
// 4. 创建逻辑设备 (Logical Device) 与队列
// ==============================================================================
void VulkanContext::CreateLogicalDevice() {
    std::set<uint32_t> uniqueQueueFamilies = {
        mGraphicsQueueFamilyIndex,
        mComputeQueueFamilyIndex,
        mPresentQueueFamilyIndex
    };

    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    float queuePriority = 1.0f;
    for (uint32_t queueFamily : uniqueQueueFamilies) {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    // 开启需要的硬件特性：线框填充、64 位整数、SSBO uint64 atomicMin（VisBuffer）
    VkPhysicalDeviceFeatures deviceFeatures{};
    deviceFeatures.fillModeNonSolid = VK_TRUE;
    deviceFeatures.shaderInt64 = VK_TRUE;

    VkPhysicalDeviceVulkan12Features features12{};
    features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features12.shaderBufferInt64Atomics = VK_TRUE;

    std::vector<const char*> deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pNext = &features12;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = deviceExtensions.data();

    VK_CHECK(vkCreateDevice(mPhysicalDevice, &createInfo, nullptr, &mDevice));

    // 获取驱动队列句柄
    vkGetDeviceQueue(mDevice, mGraphicsQueueFamilyIndex, 0, &mGraphicsQueue);
    vkGetDeviceQueue(mDevice, mComputeQueueFamilyIndex, 0, &mComputeQueue);
    vkGetDeviceQueue(mDevice, mPresentQueueFamilyIndex, 0, &mPresentQueue);
}

// ==============================================================================
// 查询表面交换链支持的模式与能力
// ==============================================================================
SwapChainSupportDetails VulkanContext::QuerySwapChainSupport(VkPhysicalDevice device) {
    SwapChainSupportDetails details;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, mSurface, &details.capabilities);

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, mSurface, &formatCount, nullptr);
    if (formatCount != 0) {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, mSurface, &formatCount, details.formats.data());
    }

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, mSurface, &presentModeCount, nullptr);
    if (presentModeCount != 0) {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, mSurface, &presentModeCount, details.presentModes.data());
    }

    return details;
}

VkSurfaceFormatKHR VulkanContext::ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) {
    for (const auto& availableFormat : availableFormats) {
        if (availableFormat.format == VK_FORMAT_B8G8R8A8_UNORM && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return availableFormat;
        }
    }
    return availableFormats[0];
}

VkPresentModeKHR VulkanContext::ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) {
    for (const auto& availablePresentMode : availablePresentModes) {
        if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return availablePresentMode;
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR; // 标准垂直同步模式
}

VkExtent2D VulkanContext::ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) {
    if (capabilities.currentExtent.width != UINT32_MAX) {
        return capabilities.currentExtent;
    } else {
        VkExtent2D actualExtent = { mWidth, mHeight };
        actualExtent.width = std::max(capabilities.minImageExtent.width, std::min(capabilities.maxImageExtent.width, actualExtent.width));
        actualExtent.height = std::max(capabilities.minImageExtent.height, std::min(capabilities.maxImageExtent.height, actualExtent.height));
        return actualExtent;
    }
}

// ==============================================================================
// 5. 创建交换链 (Swapchain)
// ==============================================================================
void VulkanContext::CreateSwapchain() {
    SwapChainSupportDetails swapChainSupport = QuerySwapChainSupport(mPhysicalDevice);

    VkSurfaceFormatKHR surfaceFormat = ChooseSwapSurfaceFormat(swapChainSupport.formats);
    VkPresentModeKHR presentMode = ChooseSwapPresentMode(swapChainSupport.presentModes);
    VkExtent2D extent = ChooseSwapExtent(swapChainSupport.capabilities);

    uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
    if (swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount) {
        imageCount = swapChainSupport.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = mSurface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    uint32_t queueFamilyIndices[] = { mGraphicsQueueFamilyIndex, mPresentQueueFamilyIndex };
    if (mGraphicsQueueFamilyIndex != mPresentQueueFamilyIndex) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;

    VK_CHECK(vkCreateSwapchainKHR(mDevice, &createInfo, nullptr, &mSwapchain));

    vkGetSwapchainImagesKHR(mDevice, mSwapchain, &imageCount, nullptr);
    mSwapchainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(mDevice, mSwapchain, &imageCount, mSwapchainImages.data());

    mSwapchainImageFormat = surfaceFormat.format;
    mSwapchainExtent = extent;
}

// ==============================================================================
// 6. 创建交换链图像视图 (ImageViews)
// ==============================================================================
void VulkanContext::CreateImageViews() {
    mSwapchainImageViews.resize(mSwapchainImages.size());

    for (size_t i = 0; i < mSwapchainImages.size(); i++) {
        VkImageViewCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = mSwapchainImages[i];
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = mSwapchainImageFormat;
        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;

        VK_CHECK(vkCreateImageView(mDevice, &createInfo, nullptr, &mSwapchainImageViews[i]));
    }
}

// ==============================================================================
// 7. 创建指令池 (Command Pool)
// ==============================================================================
void VulkanContext::CreateCommandPool() {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = mGraphicsQueueFamilyIndex;

    VK_CHECK(vkCreateCommandPool(mDevice, &poolInfo, nullptr, &mCommandPool));
}

// ==============================================================================
// 8. 分配主渲染指令缓冲 (Command Buffer)
// ==============================================================================
void VulkanContext::CreateCommandBuffers() {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = mCommandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VK_CHECK(vkAllocateCommandBuffers(mDevice, &allocInfo, &mCommandBuffer));
}

// ==============================================================================
// 9. 创建同步原语 (Sync Objects)
// ==============================================================================
void VulkanContext::CreateSyncObjects() {
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT; // 初始设为触发状态，避免第一帧等待死锁

    VK_CHECK(vkCreateSemaphore(mDevice, &semaphoreInfo, nullptr, &mImageAvailableSemaphore));
    VK_CHECK(vkCreateSemaphore(mDevice, &semaphoreInfo, nullptr, &mRenderFinishedSemaphore));
    VK_CHECK(vkCreateFence(mDevice, &fenceInfo, nullptr, &mInFlightFence));
}

// ==============================================================================
// 查找适合的物理显存类型 (如 Host Visible 用于 CPU 上传，Device Local 用于 GPU 高速读写)
// ==============================================================================
uint32_t VulkanContext::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(mPhysicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw std::runtime_error("无法找到匹配的显存类型!");
}

// ==============================================================================
// 分配并开启一次性单次执行 Command Buffer (用于上传缓冲或图像 Barrier)
// ==============================================================================
VkCommandBuffer VulkanContext::BeginSingleTimeCommands() {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = mCommandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(mDevice, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);
    return commandBuffer;
}

void VulkanContext::EndSingleTimeCommands(VkCommandBuffer commandBuffer) {
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(mGraphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(mGraphicsQueue); // 阻塞等待上传指令执行完毕

    vkFreeCommandBuffers(mDevice, mCommandPool, 1, &commandBuffer);
}

// ==============================================================================
// 开启当前帧渲染 (等待上一帧 Fence、获取 Swapchain 图像句柄、开启录制 Command Buffer)
// ==============================================================================
void VulkanContext::BeginFrame() {
    vkWaitForFences(mDevice, 1, &mInFlightFence, VK_TRUE, UINT64_MAX);
    vkResetFences(mDevice, 1, &mInFlightFence);

    VkResult result = vkAcquireNextImageKHR(mDevice, mSwapchain, UINT64_MAX, mImageAvailableSemaphore, VK_NULL_HANDLE, &mCurrentImageIndex);
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("获取交换链图像失败!");
    }

    vkResetCommandBuffer(mCommandBuffer, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    VK_CHECK(vkBeginCommandBuffer(mCommandBuffer, &beginInfo));
}

// ==============================================================================
// 结束当前帧渲染 (结束 Command Buffer 录制、提交队列并发起 Swapchain Present 呈现)
// ==============================================================================
void VulkanContext::EndFrame() {
    VK_CHECK(vkEndCommandBuffer(mCommandBuffer));

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = { mImageAvailableSemaphore };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;

    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &mCommandBuffer;

    VkSemaphore signalSemaphores[] = { mRenderFinishedSemaphore };
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    VK_CHECK(vkQueueSubmit(mGraphicsQueue, 1, &submitInfo, mInFlightFence));

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;

    VkSwapchainKHR swapchains[] = { mSwapchain };
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapchains;
    presentInfo.pImageIndices = &mCurrentImageIndex;

    vkQueuePresentKHR(mPresentQueue, &presentInfo);
}

// ==============================================================================
// 安全清理所有 Vulkan 句柄资源
// ==============================================================================
void VulkanContext::Cleanup() {
    if (mDevice != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(mDevice);

        if (mImageAvailableSemaphore != VK_NULL_HANDLE) vkDestroySemaphore(mDevice, mImageAvailableSemaphore, nullptr);
        if (mRenderFinishedSemaphore != VK_NULL_HANDLE) vkDestroySemaphore(mDevice, mRenderFinishedSemaphore, nullptr);
        if (mInFlightFence != VK_NULL_HANDLE) vkDestroyFence(mDevice, mInFlightFence, nullptr);

        if (mCommandPool != VK_NULL_HANDLE) vkDestroyCommandPool(mDevice, mCommandPool, nullptr);

        for (auto framebuffer : mSwapchainFramebuffers) {
            vkDestroyFramebuffer(mDevice, framebuffer, nullptr);
        }
        for (auto imageView : mSwapchainImageViews) {
            vkDestroyImageView(mDevice, imageView, nullptr);
        }
        if (mSwapchain != VK_NULL_HANDLE) vkDestroySwapchainKHR(mDevice, mSwapchain, nullptr);

        vkDestroyDevice(mDevice, nullptr);
        mDevice = VK_NULL_HANDLE;
    }

    if (mInstance != VK_NULL_HANDLE) {
        if (mSurface != VK_NULL_HANDLE) vkDestroySurfaceKHR(mInstance, mSurface, nullptr);
        vkDestroyInstance(mInstance, nullptr);
        mInstance = VK_NULL_HANDLE;
    }
}
