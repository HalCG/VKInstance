#ifndef FORWARD_PLUS_RENDERER_HPP
#define FORWARD_PLUS_RENDERER_HPP

// =============================================================================
// ForwardPlusRenderer — 前向+（Tile-Based Light Culling）
// =============================================================================
// 仍是一 Pass 前向着色，但片元着色器不再遍历全部光源：
//   1. CPU 端 buildForwardPlusTiles：把每盏灯的屏幕包围盒映射到 16×16 的 Tile 网格
//   2. 写入 SSBO：每 Tile 的灯数量 + 灯索引列表
//   3. forward_plus.frag 只读取当前像素所属 Tile 的灯
//
// 与 Deferred 对比：仍在前向着色，无 G-Buffer；CPU 裁剪开销换片元减负
// =============================================================================

#include "LightManager.hpp"
#include "PerfStats.hpp"
#include "RenderTypes.hpp"
#include "Scene.hpp"
#include "VulkanContext.hpp"

class ForwardPlusRenderer {
public:
    bool init(VulkanContext &ctx, VkRenderPass swapchainRenderPass);
    void shutdown();
    void bindResources(const Scene &scene, LightManager &lights);
    void render(VkCommandBuffer cmd, const Scene &scene, LightManager &lights, const FrameCamera &camera,
                VkFramebuffer framebuffer, VkExtent2D extent, PerfStats &stats, bool enableHDR);

private:
    VulkanContext *ctx_ = nullptr;
    VkRenderPass swapchainRenderPass_ = VK_NULL_HANDLE;

    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout meshSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout tileSsboLayout_ = VK_NULL_HANDLE; // 灯 SSBO + Tile 计数 + Tile 索引
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;

    VkBuffer meshUbo_ = VK_NULL_HANDLE;
    VkDeviceMemory meshUboMem_ = VK_NULL_HANDLE;
    void *meshUboMapped_ = nullptr;

    VkBuffer fragUbo_ = VK_NULL_HANDLE;
    VkDeviceMemory fragUboMem_ = VK_NULL_HANDLE;
    void *fragUboMapped_ = nullptr;

    VkDescriptorSet meshSet_ = VK_NULL_HANDLE;
    VkDescriptorSet tileSet_ = VK_NULL_HANDLE;
};

#endif
