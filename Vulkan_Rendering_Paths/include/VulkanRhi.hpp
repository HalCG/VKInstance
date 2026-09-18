#ifndef VULKAN_RHI_HPP
#define VULKAN_RHI_HPP

// =============================================================================
// VulkanRhi — 本 Demo 的轻量渲染辅助层
// =============================================================================
//
// 【Vulkan 对象依赖链 — 创建顺序建议】
//
//   ShaderModule (SPIR-V)
//        ↓
//   DescriptorSetLayout  ← 定义 Shader binding 槽位（与 .spv 中 layout 一致）
//        ↓
//   PipelineLayout         ← Layout + PushConstant 范围
//        ↓
//   RenderPass             ← 附件格式、Load/Store、Subpass、Layout 转换
//        ↓
//   Framebuffer            ← 把 ImageView 绑到 RenderPass 的 attachment 槽
//        ↓
//   GraphicsPipeline       ← 必须与 RenderPass 格式兼容（同 attachment 数/格式）
//
// 【Descriptor 三件套】
//   Layout  — 「有哪些 binding、什么类型、哪个 Shader 阶段可见」
//   Pool    — 预分配一定数量的 UBO/纹理/SSBO 描述符
//   Set     — 从 Pool 分配，再用 vkUpdateDescriptorSets 绑定具体 Buffer/Image
//
// 【Image Layout — 为何需要 cmdTransitionImage】
//   GPU 同一张图在不同用途下 layout 不同：
//     COLOR_ATTACHMENT_OPTIMAL      作为颜色附件写入
//     DEPTH_STENCIL_ATTACHMENT_OPTIMAL 深度测试写入
//     SHADER_READ_ONLY_OPTIMAL      片元着色器采样
//     PRESENT_SRC_KHR               呈现到屏幕
//   RenderPass 可自动做部分转换；跨 Pass 或上传纹理时需手动 Barrier。
//
// 【各 Renderer 使用的 Shader 与 Descriptor 对照表】
//
//   mesh.vert (共用)
//     set0 binding0: UBO { mat4 view; mat4 proj; }
//     push constant: mat4 model
//
//   forward.frag / forward_plus.frag
//     set0 binding1: combined image sampler (diffuse)
//     set0 binding2: UBO { cameraPos, materialK, lightCount, ... }
//     set1 binding0: SSBO PointLight[]  (Forward / Deferred 光照)
//     set1 binding0-2: SSBO lights + tileCount + tileIndex (Forward+)
//
//   geometry.frag (Deferred Pass1)
//     set0 binding1: diffuse 纹理
//     输出 location0-2: gAlbedo, gNormal, gPosition（C++ 侧第三张 RT 变量名 gMaterial_）
//
//   deferred_lighting.frag (Deferred Pass2)
//     set0 binding0: LightingUbo (invView, invProj, cameraPos, lightCount, enableHDR)
//     set0 binding1-4: G-Buffer 纹理 (albedo/normal/worldPos/depth)
//     set1 binding0: SSBO PointLight[]
//
//   gbuffer_debug.frag
//     set0 binding1-3: 直接采样三张 G-Buffer 颜色附件（调试用）
//
// =============================================================================

#include "VulkanContext.hpp"
#include "VulkanShader.hpp"

#include <string>
#include <vector>

namespace VulkanRhi {

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
    uint32_t colorAttachmentCount = 1;
    VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;
};

VkShaderModule loadSpirv(VulkanContext &ctx, const std::string &path);
VkCommandBuffer beginSingleTimeCommands(VulkanContext &ctx);
void endSingleTimeCommands(VulkanContext &ctx, VkCommandBuffer cmd);
void cmdTransitionImage(VkCommandBuffer cmd, VkImage image, VkFormat format, VkImageLayout oldLayout,
                        VkImageLayout newLayout, uint32_t mipLevels = 1);

VkRenderPass createSwapchainRenderPass(VulkanContext &ctx);
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

// 仅在 init/resize 时调用；勿每帧更新（见 RenderingPathsApp.hpp）
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

} // namespace VulkanRhi

#endif
