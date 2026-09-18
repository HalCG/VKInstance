#pragma once

#include "vulkan_context.h"
#include <string>
#include <vector>

// ==============================================================================
// Vulkan Buffer 显存缓冲区封装类 (用于 SSBO、UBO、Staging Buffer 分配与内存映射)
// ==============================================================================
class VulkanBuffer {
public:
    VulkanBuffer() = default;
    ~VulkanBuffer() { Destroy(); }

    // 创建 Vulkan 缓冲区及其关联的 VkDeviceMemory 显存分配
    void Create(VulkanContext* ctx, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties);
    
    // CPU 数据上传到 GPU 缓冲区 (若为 Device Local 则自动通过 Staging Buffer 转运)
    void Upload(const void* data, VkDeviceSize size);
    
    // 销毁并释放 Buffer 与 Memory
    void Destroy();

public:
    VulkanContext* mCtx = nullptr;
    VkBuffer mBuffer = VK_NULL_HANDLE;              // Vulkan 缓冲区句柄
    VkDeviceMemory mMemory = VK_NULL_HANDLE;        // 绑定的物理显存句柄
    VkDeviceSize mSize = 0;                         // 缓冲区大小 (字节)
    void* mMapped = nullptr;                        // CPU 可用映射指针 (Host Visible 时非空)
};

// ==============================================================================
// Vulkan 图像资源封装类 (用于 2D Storage Image 与 Sampler 抽样器)
// ==============================================================================
class VulkanImage {
public:
    VulkanImage() = default;
    ~VulkanImage() { Destroy(); }

    // 创建适用于 Compute Shader 读写的 2D Storage Image (RGBA32F / RGBA8)
    void CreateStorageImage(VulkanContext* ctx, uint32_t width, uint32_t height, VkFormat format);

    // 创建仅作颜色附件的离屏图像（HWRasterize 等不写 Swapchain 的图形 Pass）
    void CreateColorAttachment(VulkanContext* ctx, uint32_t width, uint32_t height, VkFormat format);
    
    // 销毁图像、图像视图与抽样器
    void Destroy();

public:
    VulkanContext* mCtx = nullptr;
    VkImage mImage = VK_NULL_HANDLE;                // 2D 图像句柄
    VkDeviceMemory mMemory = VK_NULL_HANDLE;        // 物理显存句柄
    VkImageView mImageView = VK_NULL_HANDLE;        // 图像视图句柄
    VkSampler mSampler = VK_NULL_HANDLE;            // 采样器句柄
    uint32_t mWidth = 0;                            // 宽度
    uint32_t mHeight = 0;                           // 高度
    VkFormat mFormat = VK_FORMAT_UNDEFINED;         // 像素格式
};

// 辅助函数：从磁盘二进制文件加载 SPIR-V 二进制并创建 VkShaderModule
VkShaderModule LoadSPIRVShaderModule(VkDevice device, const char* spvPath);
