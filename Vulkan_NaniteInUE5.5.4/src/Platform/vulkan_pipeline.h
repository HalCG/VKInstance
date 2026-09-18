#pragma once

#include "vulkan_utils.h"
#include <vector>

// ==============================================================================
// Vulkan 计算管线封装类 (用于管理 CS Compute Shader、Descriptor Layout/Set 与 Dispatch)
// ==============================================================================
class VulkanComputePipeline {
public:
    VulkanComputePipeline() = default;
    ~VulkanComputePipeline() { Destroy(); }

    // 创建计算管线 (加载 SPIR-V 字节码, 创建 DescriptorSetLayout, PipelineLayout 和 VkPipeline)
    void Create(VulkanContext* ctx, const char* spvPath, const std::vector<VkDescriptorSetLayoutBinding>& bindings);
    
    // 绑定 SSBO 存储缓冲区资源到指定 Binding 槽位
    void BindSSBO(uint32_t binding, VkBuffer buffer, VkDeviceSize size = VK_WHOLE_SIZE);
    
    // 绑定 UBO 统一常量缓冲区到指定 Binding 槽位
    void BindUniformBuffer(uint32_t binding, VkBuffer buffer, VkDeviceSize size);
    
    // 绑定 Storage Image 到指定 Binding 槽位
    void BindStorageImage(uint32_t binding, VkImageView imageView);
    
    // 派发 Compute Shader 工作组 (Dispatch)
    void Dispatch(VkCommandBuffer cmd, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ);
    
    // 销毁并释放 Pipeline 与 Descriptor 句柄
    void Destroy();

public:
    VulkanContext* mCtx = nullptr;
    VkShaderModule mShaderModule = VK_NULL_HANDLE;              // CS 着色器模块
    VkDescriptorSetLayout mDescriptorSetLayout = VK_NULL_HANDLE;// DescriptorSet 布局
    VkDescriptorPool mDescriptorPool = VK_NULL_HANDLE;          // Descriptor 池
    VkDescriptorSet mDescriptorSet = VK_NULL_HANDLE;            // Descriptor 实例集合
    VkPipelineLayout mPipelineLayout = VK_NULL_HANDLE;          // 管线布局
    VkPipeline mPipeline = VK_NULL_HANDLE;                      // Compute Pipeline 句柄
};

// ==============================================================================
// Vulkan 图形管线封装类 (用于硬件光栅化 HWRasterize 与全屏 FSQ 呈现)
// ==============================================================================
class VulkanGraphicsPipeline {
public:
    VulkanGraphicsPipeline() = default;
    ~VulkanGraphicsPipeline() { Destroy(); }

    // 创建 Nanite 硬件光栅化图形管线 (无颜色写掩码, 由 FS 手动 atomicMin 写入 VisBuffer64)
    void CreateHWRasterize(VulkanContext* ctx, const char* vertSpv, const char* fragSpv, const std::vector<VkDescriptorSetLayoutBinding>& bindings);
    
    // 创建全屏 FSQ 呈现图形管线 (从 VisualizationTexture 采样并写入 Swapchain 颜色缓冲)
    void CreateFSQ(VulkanContext* ctx, const char* vertSpv, const char* fragSpv, const std::vector<VkDescriptorSetLayoutBinding>& bindings);
    
    // 资源绑定辅助函数
    void BindSSBO(uint32_t binding, VkBuffer buffer, VkDeviceSize size = VK_WHOLE_SIZE);
    void BindUniformBuffer(uint32_t binding, VkBuffer buffer, VkDeviceSize size);
    void BindCombinedImageSampler(uint32_t binding, VkImageView imageView, VkSampler sampler);

    // 开始/结束一次图形 RenderPass（HWRasterize 用离屏 FB，FSQ 用 Swapchain FB）
    void BeginRenderPass(VkCommandBuffer cmd, VkFramebuffer framebuffer, VkExtent2D extent, bool clearColor, const float clearColorRGBA[4] = nullptr);
    void EndRenderPass(VkCommandBuffer cmd);
    void Draw(VkCommandBuffer cmd, uint32_t vertexCount, uint32_t instanceCount = 1);
    void DrawIndirect(VkCommandBuffer cmd, VkBuffer indirectBuffer, VkDeviceSize offset = 0);

    // 销毁并释放图形管线资源
    void Destroy();

public:
    VulkanContext* mCtx = nullptr;
    VkShaderModule mVertModule = VK_NULL_HANDLE;                // 顶点着色器模块
    VkShaderModule mFragModule = VK_NULL_HANDLE;                // 片段着色器模块
    VkDescriptorSetLayout mDescriptorSetLayout = VK_NULL_HANDLE;// Descriptor Layout
    VkDescriptorPool mDescriptorPool = VK_NULL_HANDLE;          // Descriptor Pool
    VkDescriptorSet mDescriptorSet = VK_NULL_HANDLE;            // Descriptor Set
    VkRenderPass mRenderPass = VK_NULL_HANDLE;                  // 渲染通道句柄
    VkFramebuffer mFramebuffer = VK_NULL_HANDLE;                // HWRasterize 离屏 FB；FSQ 仍用 ctx->mSwapchainFramebuffers
    VulkanImage mOffscreenColor;                                // HWRasterize 占位颜色附件（colorWriteMask=0，仅满足 RenderPass）
    VkPipelineLayout mPipelineLayout = VK_NULL_HANDLE;          // 管线布局
    VkPipeline mPipeline = VK_NULL_HANDLE;                      // Graphics Pipeline 句柄
};
