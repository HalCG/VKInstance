#ifndef FORWARD_RENDERER_HPP
#define FORWARD_RENDERER_HPP

// =============================================================================
// ForwardRenderer — 经典前向渲染
// =============================================================================
// 单 Pass 直接画到 Swapchain Framebuffer：
//   顶点着色器：mesh.vert（view/proj 来自 UBO，model 来自 Push Constant）
//   片元着色器：forward.frag（对每个像素循环所有点光源，Blinn-Phong）
//
// Descriptor Set 布局：
//   set0 = mesh（UBO + 纹理 + 片元 UBO）
//   set1 = 点光源 SSBO（std430，最多 512 盏）
//
// 优点：实现简单、透明物体友好
// 缺点：灯数量 × 像素数，复杂场景片元成本高
// =============================================================================

#include "LightManager.hpp"
#include "PerfStats.hpp"
#include "RenderTypes.hpp"
#include "Scene.hpp"
#include "VulkanContext.hpp"

class ForwardRenderer {
public:
    bool init(VulkanContext &ctx, VkRenderPass swapchainRenderPass);
    void shutdown();
    // 在 init 之后调用一次：把 UBO/纹理/SSBO 写入 Descriptor（勿每帧调用）
    void bindResources(const Scene &scene, LightManager &lights);
    void render(VkCommandBuffer cmd, const Scene &scene, LightManager &lights, const FrameCamera &camera,
                VkFramebuffer framebuffer, VkExtent2D extent, PerfStats &stats, bool enableHDR);

private:
    VulkanContext *ctx_ = nullptr;
    VkRenderPass swapchainRenderPass_ = VK_NULL_HANDLE;

    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout meshSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout lightSsboLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;

    VkBuffer meshUbo_ = VK_NULL_HANDLE;
    VkDeviceMemory meshUboMem_ = VK_NULL_HANDLE;
    void *meshUboMapped_ = nullptr; // HOST_VISIBLE：每帧 memcpy 相机矩阵

    VkBuffer fragUbo_ = VK_NULL_HANDLE;
    VkDeviceMemory fragUboMem_ = VK_NULL_HANDLE;
    void *fragUboMapped_ = nullptr;

    VkDescriptorSet meshSet_ = VK_NULL_HANDLE;
    VkDescriptorSet lightSet_ = VK_NULL_HANDLE;
};

#endif
