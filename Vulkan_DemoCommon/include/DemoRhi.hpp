#ifndef DEMO_RHI_HPP
#define DEMO_RHI_HPP

// =============================================================================
// DemoRhi — Demo 共用渲染辅助（AA / OIT 等）
// =============================================================================
// 在 VulkanRhi 基础上扩展：
//   createOffscreenRenderPass — 可采样离屏颜色（FXAA/TAA/OIT）
//   createMsaaRenderPass      — 多采样颜色 + Resolve + 多采样深度
//   createMsaaFramebuffer       — 每 Swapchain 图像一个 MSAA FB（Resolve 目标不同）
//   getMaxUsableSampleCount     — 查询 GPU 支持的 MSAA 档位
// =============================================================================

#include "VulkanContext.hpp"
#include "VulkanShader.hpp"

#include <string>
#include <vector>

namespace DemoRhi {

struct PipelineInfo {
    VkShaderModule vert = VK_NULL_HANDLE;
    VkShaderModule frag = VK_NULL_HANDLE;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkExtent2D extent{};
    const VkVertexInputBindingDescription *bindings = nullptr;
    uint32_t bindingCount = 0;
    const VkVertexInputAttributeDescription *attributes = nullptr;
    uint32_t attributeCount = 0;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    bool depthTest = true;
    bool depthWrite = true;
    uint32_t colorAttachmentCount = 1;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    bool alphaBlend = false;
    bool oitFrontToBackBlend = false;
    VkCullModeFlags cullMode = VK_CULL_MODE_NONE;
};

VkShaderModule loadSpirv(VulkanContext &ctx, const std::string &path);
VkCommandBuffer beginSingleTimeCommands(VulkanContext &ctx);
void endSingleTimeCommands(VulkanContext &ctx, VkCommandBuffer cmd);

void cmdTransitionImage(VkCommandBuffer cmd, VkImage image, VkFormat format, VkImageLayout oldLayout,
                        VkImageLayout newLayout, uint32_t mipLevels = 1);

VkRenderPass createSwapchainRenderPass(VulkanContext &ctx);
VkRenderPass createSwapchainLoadRenderPass(VulkanContext &ctx);
VkRenderPass createSwapchainPeelRenderPass(VulkanContext &ctx);
VkRenderPass createMsaaRenderPass(VulkanContext &ctx, VkSampleCountFlagBits samples);
VkRenderPass createOffscreenRenderPass(VulkanContext &ctx, VkFormat colorFormat);
VkFramebuffer createFramebuffer(VulkanContext &ctx, VkRenderPass renderPass, VkImageView colorView, VkImageView depthView,
                                VkExtent2D extent);
VkPipeline createGraphicsPipeline(VulkanContext &ctx, const PipelineInfo &info);

VkDescriptorSetLayout createMeshDescriptorLayout(VulkanContext &ctx);
VkDescriptorSetLayout createLightingDescriptorLayout(VulkanContext &ctx);
VkDescriptorSetLayout createDebugDescriptorLayout(VulkanContext &ctx);
VkDescriptorSetLayout createLightSsboLayout(VulkanContext &ctx);
VkDescriptorSetLayout createForwardPlusSsboLayout(VulkanContext &ctx);

VkDescriptorPool createDescriptorPool(VulkanContext &ctx, uint32_t setCount);
VkDescriptorSet allocateSet(VulkanContext &ctx, VkDescriptorPool pool, VkDescriptorSetLayout layout);

void writeMeshDescriptor(VulkanContext &ctx, VkDescriptorSet set, VkBuffer ubo, VkImageView texture,
                         VkSampler sampler);
void writeLightingDescriptor(VulkanContext &ctx, VkDescriptorSet set, VkBuffer ubo,
                             VkImageView albedo, VkImageView normal, VkImageView material, VkImageView depth,
                             VkSampler sampler);
void writeDebugDescriptor(VulkanContext &ctx, VkDescriptorSet set, VkBuffer ubo,
                          VkImageView albedo, VkImageView normal, VkImageView material, VkSampler sampler);
void writeLightSsboDescriptor(VulkanContext &ctx, VkDescriptorSet set, VkBuffer lightBuffer);
void writeForwardPlusSsboDescriptor(VulkanContext &ctx, VkDescriptorSet set, VkBuffer lightBuffer,
                                    VkBuffer tileCountBuffer, VkBuffer tileIndexBuffer);

VkDescriptorSetLayout createUboDescriptorLayout(VulkanContext &ctx);
VkDescriptorSetLayout createTextureDescriptorLayout(VulkanContext &ctx);
VkDescriptorSetLayout createPeelDescriptorLayout(VulkanContext &ctx);
VkDescriptorSetLayout createOitBufferDescriptorLayout(VulkanContext &ctx);
VkDescriptorSetLayout createOitMeshDescriptorLayout(VulkanContext &ctx);

void writeUboDescriptor(VulkanContext &ctx, VkDescriptorSet set, VkBuffer ubo);
void writeTextureDescriptor(VulkanContext &ctx, VkDescriptorSet set, VkImageView view, VkSampler sampler);
void writePeelDescriptor(VulkanContext &ctx, VkDescriptorSet set, VkBuffer ubo, VkImageView depthView, VkSampler sampler);
void writeOitBufferDescriptor(VulkanContext &ctx, VkDescriptorSet set, VkBuffer headBuffer, VkBuffer nodeBuffer);
void writeOitMeshDescriptor(VulkanContext &ctx, VkDescriptorSet set, VkBuffer ubo, VkBuffer headBuffer,
                            VkBuffer nodeBuffer);

VkFramebuffer createMsaaFramebuffer(VulkanContext &ctx, VkRenderPass renderPass, VkImageView msaaColor,
                                    VkImageView resolveColor, VkImageView depthView, VkExtent2D extent);
VkSampler createLinearSampler(VulkanContext &ctx);
VkSampleCountFlagBits getMaxUsableSampleCount(VulkanContext &ctx);

} // namespace DemoRhi

#endif // DEMO_RHI_HPP
