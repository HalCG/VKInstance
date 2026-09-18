#ifndef DEFERRED_RENDERER_HPP
#define DEFERRED_RENDERER_HPP

// =============================================================================
// DeferredRenderer — 延迟渲染（两 Pass）
// =============================================================================
// Pass 1 — 几何 Pass（离屏 G-Buffer RenderPass）：
//   输出 albedo(RGBA8)、normal(RGBA16F)、worldPos(RGBA16F)、depth(D32)
//   （C++ 侧第三张 RT 变量名为 gMaterial_，与 geometry.frag 的 gPosition 对应）
//   使用 geometry.frag，与 Forward 共用 mesh.vert
//
// Pass 2 — 光照 Pass（Swapchain RenderPass）：
//   全屏三角形 deferred_lighting.vert/frag，采样 G-Buffer + 点光源 SSBO
//   从 depth 重建世界坐标，再做 Blinn-Phong
//   按 G 可切换 gbuffer_debug.frag 查看 G-Buffer
//
// G-Buffer 与 Swapchain 是两次独立的 vkCmdBeginRenderPass；
// 几何 Pass 结束后需 Pipeline Barrier，确保附件可被片元着色器采样。
// =============================================================================

#include "LightManager.hpp"
#include "PerfStats.hpp"
#include "RenderTypes.hpp"
#include "Scene.hpp"
#include "VulkanContext.hpp"

class DeferredRenderer {
public:
    bool init(VulkanContext &ctx, VkRenderPass swapchainRenderPass);
    void shutdown();
    void bindResources(const Scene &scene, LightManager &lights);
    void resize(int width, int height); // 窗口尺寸变化时重建 G-Buffer 与管线
    void render(VkCommandBuffer cmd, const Scene &scene, LightManager &lights, const FrameCamera &camera,
                VkFramebuffer swapchainFb, VkExtent2D extent, PerfStats &stats, bool showGBufferDebug, bool enableHDR);

    VkRenderPass gBufferRenderPass() const { return gBufferRenderPass_; }

private:
    void createGBufferRenderPass();
    void createGBufferAttachments(int width, int height);
    void destroyGBufferAttachments();
    void createPipelines();
    void updateGBufferDescriptors(); // G-Buffer ImageView 变化时（resize）更新光照/调试 Descriptor

    VulkanContext *ctx_ = nullptr;
    VkRenderPass swapchainRenderPass_ = VK_NULL_HANDLE;
    int width_ = 0;
    int height_ = 0;

    // --- G-Buffer 离屏附件 ---
    VkRenderPass gBufferRenderPass_ = VK_NULL_HANDLE;
    VkFramebuffer gBufferFramebuffer_ = VK_NULL_HANDLE;
    VkImage gAlbedo_ = VK_NULL_HANDLE;
    VkImage gNormal_ = VK_NULL_HANDLE;
    VkImage gMaterial_ = VK_NULL_HANDLE;
    VkImage gDepth_ = VK_NULL_HANDLE;
    VkDeviceMemory gAlbedoMem_ = VK_NULL_HANDLE;
    VkDeviceMemory gNormalMem_ = VK_NULL_HANDLE;
    VkDeviceMemory gMaterialMem_ = VK_NULL_HANDLE;
    VkDeviceMemory gDepthMem_ = VK_NULL_HANDLE;
    VkImageView gAlbedoView_ = VK_NULL_HANDLE;
    VkImageView gNormalView_ = VK_NULL_HANDLE;
    VkImageView gMaterialView_ = VK_NULL_HANDLE;
    VkImageView gDepthView_ = VK_NULL_HANDLE;
    VkSampler gBufferSampler_ = VK_NULL_HANDLE;

    VkPipeline geometryPipeline_ = VK_NULL_HANDLE;
    VkPipeline lightingPipeline_ = VK_NULL_HANDLE;
    VkPipeline debugPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout geometryLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout lightingLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout debugLayout_ = VK_NULL_HANDLE;

    VkDescriptorSetLayout meshSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout lightingSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout debugSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout lightSsboLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;

    VkBuffer meshUbo_ = VK_NULL_HANDLE;
    VkDeviceMemory meshUboMem_ = VK_NULL_HANDLE;
    void *meshUboMapped_ = nullptr;

    VkBuffer lightingUbo_ = VK_NULL_HANDLE;
    VkDeviceMemory lightingUboMem_ = VK_NULL_HANDLE;
    void *lightingUboMapped_ = nullptr;

    VkBuffer debugUbo_ = VK_NULL_HANDLE;
    VkDeviceMemory debugUboMem_ = VK_NULL_HANDLE;
    void *debugUboMapped_ = nullptr;

    VkDescriptorSet geometryMeshSet_ = VK_NULL_HANDLE;
    VkDescriptorSet lightingSet_ = VK_NULL_HANDLE;
    VkDescriptorSet lightingLightSet_ = VK_NULL_HANDLE;
    VkDescriptorSet debugSet_ = VK_NULL_HANDLE;
};

#endif
