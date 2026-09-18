// =============================================================================
// DeferredRenderer 实现 — 经典两 Pass 延迟渲染
// =============================================================================
//
// 【数据流】
//   Pass1 (G-Buffer RP, 离屏)
//     mesh.vert + geometry.frag
//     → RT0: albedo   (RGBA8)
//     → RT1: normal   (RGBA16F, xyz=法线)
//     → RT2: world position (RGBA16F, geometry.frag 的 gPosition；C++ 变量名 gMaterial_)
//     → depth         (D32, 供 Pass2 重建世界坐标)
//
//   [Pipeline Barrier] 几何写入 → 光照片元可读
//
//   Pass2 (Swapchain RP)
//     deferred_lighting.vert (全屏三角形, 3 顶点)
//     + deferred_lighting.frag / gbuffer_debug.frag
//     → 输出到 Swapchain 颜色附件
//
// 【为何 model 用 Push Constant 而非 UBO】
//   地面 + 12 个立方体 = 13 次 draw，若 UBO 只存一个 model，每次 draw 前
//   都要覆写并 flush，且曾出现多物体互相覆盖的问题；Push Constant 专为
//   小数据、每 draw 变化而设计，开销更低。
//
// =============================================================================

#include "DeferredRenderer.hpp"
#include "AppConfig.hpp"
#include "VulkanBuffer.hpp"
#include "VulkanRhi.hpp"

#include <glm/gtc/matrix_inverse.hpp>
#include <array>
#include <cstring>

namespace {
struct LightingUboData {
    glm::vec4 cameraPos;
    glm::mat4 invView;
    glm::mat4 invProj;
    int lightCount;
    int enableHDR;
};

struct DebugUboData {
    int debugMode;
};

std::array<VkVertexInputBindingDescription, 1> meshBindings() {
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = 32;
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return {binding};
}

std::array<VkVertexInputAttributeDescription, 3> meshAttributes() {
    return std::array<VkVertexInputAttributeDescription, 3>{
        VkVertexInputAttributeDescription{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},
        VkVertexInputAttributeDescription{1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12},
        VkVertexInputAttributeDescription{2, 0, VK_FORMAT_R32G32_SFLOAT, 24},
    };
}
} // namespace

bool DeferredRenderer::init(VulkanContext &ctx, VkRenderPass swapchainRenderPass) {
    ctx_ = &ctx;
    swapchainRenderPass_ = swapchainRenderPass; // Pass2 光照画到与 Forward 相同的 Swapchain FB

    // --- 1. Descriptor Set Layout（定义 Shader 资源槽位）---
    meshSetLayout_ = VulkanRhi::createMeshDescriptorLayout(ctx);
    lightingSetLayout_ = VulkanRhi::createLightingDescriptorLayout(ctx);
    debugSetLayout_ = VulkanRhi::createDebugDescriptorLayout(ctx);
    lightSsboLayout_ = VulkanRhi::createLightSsboLayout(ctx);

    // --- 2. PipelineLayout = DescriptorSetLayout + PushConstant 范围 ---
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushRange.size = sizeof(glm::mat4); // 对应 mesh.vert 的 push.model

    VkDescriptorSetLayout layouts[] = {meshSetLayout_};
    VkPipelineLayoutCreateInfo geomLayoutInfo{};
    geomLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    geomLayoutInfo.setLayoutCount = 1;
    geomLayoutInfo.pSetLayouts = layouts;
    geomLayoutInfo.pushConstantRangeCount = 1;
    geomLayoutInfo.pPushConstantRanges = &pushRange;
    vkCreatePipelineLayout(ctx.device(), &geomLayoutInfo, nullptr, &geometryLayout_);

    VkDescriptorSetLayout lightLayouts[] = {lightingSetLayout_, lightSsboLayout_};
    VkPipelineLayoutCreateInfo lightLayoutInfo{};
    lightLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    lightLayoutInfo.setLayoutCount = 2;
    lightLayoutInfo.pSetLayouts = lightLayouts;
    vkCreatePipelineLayout(ctx.device(), &lightLayoutInfo, nullptr, &lightingLayout_);

    VkDescriptorSetLayout debugLayouts[] = {debugSetLayout_};
    VkPipelineLayoutCreateInfo debugLayoutInfo{};
    debugLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    debugLayoutInfo.setLayoutCount = 1;
    debugLayoutInfo.pSetLayouts = debugLayouts;
    vkCreatePipelineLayout(ctx.device(), &debugLayoutInfo, nullptr, &debugLayout_);

    descriptorPool_ = VulkanRhi::createDescriptorPool(ctx, 8);

    // --- 3. HOST_VISIBLE UBO：每帧 CPU memcpy，无需 staging buffer ---
    // mesh UBO: view(64) + proj(64) = 128 字节
    VulkanUtil::createBuffer(ctx, 128, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, meshUbo_,
                               meshUboMem_);
    vkMapMemory(ctx.device(), meshUboMem_, 0, 128, 0, &meshUboMapped_);

    VulkanUtil::createBuffer(ctx, 160, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                               lightingUbo_, lightingUboMem_);
    vkMapMemory(ctx.device(), lightingUboMem_, 0, 160, 0, &lightingUboMapped_);

    VulkanUtil::createBuffer(ctx, 16, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, debugUbo_,
                               debugUboMem_);
    vkMapMemory(ctx.device(), debugUboMem_, 0, 16, 0, &debugUboMapped_);

    geometryMeshSet_ = VulkanRhi::allocateSet(ctx, descriptorPool_, meshSetLayout_);
    lightingSet_ = VulkanRhi::allocateSet(ctx, descriptorPool_, lightingSetLayout_);
    lightingLightSet_ = VulkanRhi::allocateSet(ctx, descriptorPool_, lightSsboLayout_);
    debugSet_ = VulkanRhi::allocateSet(ctx, descriptorPool_, debugSetLayout_);

    // G-Buffer 采样用 NEAREST：每像素对应 texel，不做插值模糊
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    vkCreateSampler(ctx.device(), &samplerInfo, nullptr, &gBufferSampler_);

    createGBufferRenderPass();
    createPipelines();
    createGBufferAttachments(AppConfig::kInitialWidth, AppConfig::kInitialHeight);
    return true;
}

void DeferredRenderer::bindResources(const Scene &scene, LightManager &lights) {
    VulkanRhi::writeMeshDescriptor(*ctx_, geometryMeshSet_, meshUbo_, scene.whiteTextureView(), scene.sampler());
    VulkanRhi::writeLightSsboDescriptor(*ctx_, lightingLightSet_, lights.lightBuffer());
}

void DeferredRenderer::updateGBufferDescriptors() {
    VulkanRhi::writeLightingDescriptor(*ctx_, lightingSet_, lightingUbo_, gAlbedoView_, gNormalView_, gMaterialView_,
                                       gDepthView_, gBufferSampler_);
    VulkanRhi::writeDebugDescriptor(*ctx_, debugSet_, debugUbo_, gAlbedoView_, gNormalView_, gMaterialView_,
                                    gBufferSampler_);
}

void DeferredRenderer::createPipelines() {
    const auto bindings = meshBindings();
    const auto attrs = meshAttributes();
    VkExtent2D extent{static_cast<uint32_t>(width_ > 0 ? width_ : AppConfig::kInitialWidth),
                      static_cast<uint32_t>(height_ > 0 ? height_ : AppConfig::kInitialHeight)};

    auto geomVert = VulkanRhi::loadSpirv(*ctx_, AppConfig::shaderPath("mesh_vert.spv"));
    auto geomFrag = VulkanRhi::loadSpirv(*ctx_, AppConfig::shaderPath("geometry_frag.spv"));
    auto lightVert = VulkanRhi::loadSpirv(*ctx_, AppConfig::shaderPath("deferred_lighting_vert.spv"));
    auto lightFrag = VulkanRhi::loadSpirv(*ctx_, AppConfig::shaderPath("deferred_lighting_frag.spv"));
    auto debugFrag = VulkanRhi::loadSpirv(*ctx_, AppConfig::shaderPath("gbuffer_debug_frag.spv"));

    VulkanRhi::PipelineInfo geomInfo{};
    geomInfo.vert = geomVert;
    geomInfo.frag = geomFrag;
    geomInfo.renderPass = gBufferRenderPass_;
    geomInfo.extent = extent;
    geomInfo.bindings = bindings.data();
    geomInfo.bindingCount = 1;
    geomInfo.attributes = attrs.data();
    geomInfo.attributeCount = 3;
    geomInfo.layout = geometryLayout_;
    geomInfo.depthTest = true;
    geomInfo.colorAttachmentCount = 3;
    geomInfo.cullMode = VK_CULL_MODE_NONE;
    geometryPipeline_ = VulkanRhi::createGraphicsPipeline(*ctx_, geomInfo);

    VulkanRhi::PipelineInfo lightInfo{};
    lightInfo.vert = lightVert;
    lightInfo.frag = lightFrag;
    lightInfo.renderPass = swapchainRenderPass_;
    lightInfo.extent = extent;
    lightInfo.layout = lightingLayout_;
    lightInfo.depthTest = false;
    lightInfo.cullMode = VK_CULL_MODE_NONE;
    lightingPipeline_ = VulkanRhi::createGraphicsPipeline(*ctx_, lightInfo);

    VulkanRhi::PipelineInfo debugInfo{};
    debugInfo.vert = lightVert;
    debugInfo.frag = debugFrag;
    debugInfo.renderPass = swapchainRenderPass_;
    debugInfo.extent = extent;
    debugInfo.layout = debugLayout_;
    debugInfo.depthTest = false;
    debugInfo.cullMode = VK_CULL_MODE_NONE;
    debugPipeline_ = VulkanRhi::createGraphicsPipeline(*ctx_, debugInfo);

    vkDestroyShaderModule(ctx_->device(), geomVert, nullptr);
    vkDestroyShaderModule(ctx_->device(), geomFrag, nullptr);
    vkDestroyShaderModule(ctx_->device(), lightVert, nullptr);
    vkDestroyShaderModule(ctx_->device(), lightFrag, nullptr);
    vkDestroyShaderModule(ctx_->device(), debugFrag, nullptr);
}

// 创建 G-Buffer 专用 RenderPass（与 Swapchain RP 独立）
// 附件 finalLayout 设为 SHADER_READ_ONLY，Pass 结束后可直接被光照 Pass 采样
void DeferredRenderer::createGBufferRenderPass() {
    VkAttachmentDescription attachments[4]{};
    // attachment 0: albedo
    attachments[0] = {0, VK_FORMAT_R8G8B8A8_UNORM, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR,
                      VK_ATTACHMENT_STORE_OP_STORE, VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
                      VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    // attachment 1: normal (16-bit float 精度更高)
    attachments[1] = {0, VK_FORMAT_R16G16B16A16_SFLOAT, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR,
                      VK_ATTACHMENT_STORE_OP_STORE, VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
                      VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    // attachment 2: 世界坐标 (Pass2 直接采样，无需从 depth 反投影)
    attachments[2] = attachments[1];
    // attachment 3: depth — finalLayout 为 DEPTH_READ_ONLY 以便 Pass2 采样重建位置
    attachments[3] = {0, VulkanUtil::findDepthFormat(*ctx_), VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR,
                      VK_ATTACHMENT_STORE_OP_STORE, VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
                      VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};

    VkAttachmentReference colorRefs[3] = {{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
                                          {1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
                                          {2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}};
    VkAttachmentReference depthRef{3, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 3;
    subpass.pColorAttachments = colorRefs;
    subpass.pDepthStencilAttachment = &depthRef;

    // SubpassDependency：处理「上一帧采样 G-Buffer」→「本帧写入 G-Buffer」以及
    // 「本帧写入完成」→「Pass 外片元着色器读取」的同步
    VkSubpassDependency dependencies[2]{};
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[0].dstStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[0].dstAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].srcAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    VkRenderPassCreateInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpInfo.attachmentCount = 4;
    rpInfo.pAttachments = attachments;
    rpInfo.subpassCount = 1;
    rpInfo.pSubpasses = &subpass;
    rpInfo.dependencyCount = 2;
    rpInfo.pDependencies = dependencies;
    vkCreateRenderPass(ctx_->device(), &rpInfo, nullptr, &gBufferRenderPass_);
}

// 创建/重建与窗口同尺寸的 G-Buffer 附件。
// gMaterial_ 命名来自早期「材质 RT」设计，实际存 world position（见 geometry.frag gPosition）。
void DeferredRenderer::createGBufferAttachments(int width, int height) {
    width_ = width;
    height_ = height;
    const uint32_t w = static_cast<uint32_t>(width);
    const uint32_t h = static_cast<uint32_t>(height);

    VulkanUtil::createImage(*ctx_, w, h, 1, VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL,
                            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, gAlbedo_, gAlbedoMem_);
    VulkanUtil::createImage(*ctx_, w, h, 1, VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_R16G16B16A16_SFLOAT,
                            VK_IMAGE_TILING_OPTIMAL,
                            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, gNormal_, gNormalMem_);
    VulkanUtil::createImage(*ctx_, w, h, 1, VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_R16G16B16A16_SFLOAT,
                            VK_IMAGE_TILING_OPTIMAL,
                            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, gMaterial_, gMaterialMem_);
    VulkanUtil::createImage(*ctx_, w, h, 1, VK_SAMPLE_COUNT_1_BIT, VulkanUtil::findDepthFormat(*ctx_),
                            VK_IMAGE_TILING_OPTIMAL,
                            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, gDepth_, gDepthMem_);

    gAlbedoView_ = VulkanUtil::createImageView(*ctx_, gAlbedo_, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, 1);
    gNormalView_ =
        VulkanUtil::createImageView(*ctx_, gNormal_, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT, 1);
    gMaterialView_ =
        VulkanUtil::createImageView(*ctx_, gMaterial_, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT, 1);
    gDepthView_ = VulkanUtil::createImageView(*ctx_, gDepth_, VulkanUtil::findDepthFormat(*ctx_),
                                              VK_IMAGE_ASPECT_DEPTH_BIT, 1);

    VkImageView fbAttachments[] = {gAlbedoView_, gNormalView_, gMaterialView_, gDepthView_};
    VkFramebufferCreateInfo fbInfo{};
    fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbInfo.renderPass = gBufferRenderPass_;
    fbInfo.attachmentCount = 4;
    fbInfo.pAttachments = fbAttachments;
    fbInfo.width = w;
    fbInfo.height = h;
    fbInfo.layers = 1;
    vkCreateFramebuffer(ctx_->device(), &fbInfo, nullptr, &gBufferFramebuffer_);
    updateGBufferDescriptors();
}

void DeferredRenderer::destroyGBufferAttachments() {
    if (!ctx_) {
        return;
    }
    if (gBufferFramebuffer_) {
        vkDestroyFramebuffer(ctx_->device(), gBufferFramebuffer_, nullptr);
        gBufferFramebuffer_ = VK_NULL_HANDLE;
    }
    auto destroyImage = [&](VkImageView &view, VkImage &img, VkDeviceMemory &mem) {
        if (view) {
            vkDestroyImageView(ctx_->device(), view, nullptr);
            view = VK_NULL_HANDLE;
        }
        if (img) {
            vkDestroyImage(ctx_->device(), img, nullptr);
            vkFreeMemory(ctx_->device(), mem, nullptr);
            img = VK_NULL_HANDLE;
        }
    };
    destroyImage(gAlbedoView_, gAlbedo_, gAlbedoMem_);
    destroyImage(gNormalView_, gNormal_, gNormalMem_);
    destroyImage(gMaterialView_, gMaterial_, gMaterialMem_);
    destroyImage(gDepthView_, gDepth_, gDepthMem_);
}

void DeferredRenderer::resize(int width, int height) {
    if (width == width_ && height == height_ && gBufferFramebuffer_) {
        return;
    }
    vkDeviceWaitIdle(ctx_->device());
    destroyGBufferAttachments();
    createGBufferAttachments(width, height);
}

void DeferredRenderer::shutdown() {
    destroyGBufferAttachments();
    if (ctx_) {
        if (geometryPipeline_) {
            vkDestroyPipeline(ctx_->device(), geometryPipeline_, nullptr);
            geometryPipeline_ = VK_NULL_HANDLE;
        }
        if (lightingPipeline_) {
            vkDestroyPipeline(ctx_->device(), lightingPipeline_, nullptr);
            lightingPipeline_ = VK_NULL_HANDLE;
        }
        if (debugPipeline_) {
            vkDestroyPipeline(ctx_->device(), debugPipeline_, nullptr);
            debugPipeline_ = VK_NULL_HANDLE;
        }
        if (gBufferRenderPass_) {
            vkDestroyRenderPass(ctx_->device(), gBufferRenderPass_, nullptr);
            gBufferRenderPass_ = VK_NULL_HANDLE;
        }
        if (gBufferSampler_) {
            vkDestroySampler(ctx_->device(), gBufferSampler_, nullptr);
        }
        if (descriptorPool_) {
            vkDestroyDescriptorPool(ctx_->device(), descriptorPool_, nullptr);
        }
        if (geometryLayout_) {
            vkDestroyPipelineLayout(ctx_->device(), geometryLayout_, nullptr);
        }
        if (lightingLayout_) {
            vkDestroyPipelineLayout(ctx_->device(), lightingLayout_, nullptr);
        }
        if (debugLayout_) {
            vkDestroyPipelineLayout(ctx_->device(), debugLayout_, nullptr);
        }
        for (VkDescriptorSetLayout *layout :
             {&meshSetLayout_, &lightingSetLayout_, &debugSetLayout_, &lightSsboLayout_}) {
            if (*layout) {
                vkDestroyDescriptorSetLayout(ctx_->device(), *layout, nullptr);
                *layout = VK_NULL_HANDLE;
            }
        }
        if (meshUboMapped_) {
            vkUnmapMemory(ctx_->device(), meshUboMem_);
            meshUboMapped_ = nullptr;
        }
        if (lightingUboMapped_) {
            vkUnmapMemory(ctx_->device(), lightingUboMem_);
            lightingUboMapped_ = nullptr;
        }
        if (debugUboMapped_) {
            vkUnmapMemory(ctx_->device(), debugUboMem_);
            debugUboMapped_ = nullptr;
        }
        if (meshUbo_) {
            vkDestroyBuffer(ctx_->device(), meshUbo_, nullptr);
            vkFreeMemory(ctx_->device(), meshUboMem_, nullptr);
        }
        if (lightingUbo_) {
            vkDestroyBuffer(ctx_->device(), lightingUbo_, nullptr);
            vkFreeMemory(ctx_->device(), lightingUboMem_, nullptr);
        }
        if (debugUbo_) {
            vkDestroyBuffer(ctx_->device(), debugUbo_, nullptr);
            vkFreeMemory(ctx_->device(), debugUboMem_, nullptr);
        }
    }
    ctx_ = nullptr;
}

void DeferredRenderer::render(VkCommandBuffer cmd, const Scene &scene, LightManager &lights,
                              const FrameCamera &camera, VkFramebuffer swapchainFb, VkExtent2D extent, PerfStats &stats,
                              bool showGBufferDebug, bool enableHDR) {
    resize(static_cast<int>(extent.width), static_cast<int>(extent.height));

    // ---- Pass 1：几何 Pass，写入离屏 G-Buffer ----
    stats.geometryPass.begin();

    VkClearValue clearValues[4]{};
    for (int i = 0; i < 3; ++i) {
        clearValues[i].color = {{0, 0, 0, 1}};
    }
    clearValues[3].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo gBufferBegin{};
    gBufferBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    gBufferBegin.renderPass = gBufferRenderPass_;
    gBufferBegin.framebuffer = gBufferFramebuffer_;
    gBufferBegin.renderArea.extent = extent;
    gBufferBegin.clearValueCount = 4;
    gBufferBegin.pClearValues = clearValues;
    vkCmdBeginRenderPass(cmd, &gBufferBegin, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{0, 0, static_cast<float>(extent.width), static_cast<float>(extent.height), 0, 1};
    VkRect2D scissor{{0, 0}, extent};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    scene.writeCameraUbo(meshUboMapped_, camera.view, camera.projection);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, geometryPipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, geometryLayout_, 0, 1, &geometryMeshSet_, 0, nullptr);
    scene.drawFloor(cmd, geometryLayout_, geometryPipeline_, geometryMeshSet_);
    scene.drawSpotMeshes(cmd, geometryLayout_, geometryPipeline_, geometryMeshSet_);
    vkCmdEndRenderPass(cmd);

    // 跨 RenderPass 显式 Barrier：Pass1 颜色/深度写入 → Pass2 片元采样
    // RenderPass dependency 已声明部分同步，此处再插 barrier 确保 validation 与真机行为一致
    // layout 保持 SHADER_READ_ONLY / DEPTH_READ_ONLY（RP finalLayout 已设为可读）
    VkImageMemoryBarrier gBufferBarriers[4]{};
    gBufferBarriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    gBufferBarriers[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    gBufferBarriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    gBufferBarriers[0].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    gBufferBarriers[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    gBufferBarriers[0].image = gAlbedo_;
    gBufferBarriers[0].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    gBufferBarriers[1] = gBufferBarriers[0];
    gBufferBarriers[1].image = gNormal_;
    gBufferBarriers[2] = gBufferBarriers[0];
    gBufferBarriers[2].image = gMaterial_;

    gBufferBarriers[3].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    gBufferBarriers[3].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    gBufferBarriers[3].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    gBufferBarriers[3].oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    gBufferBarriers[3].newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    gBufferBarriers[3].image = gDepth_;
    gBufferBarriers[3].subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};

    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 4, gBufferBarriers);

    stats.frameStats().geometryPassMs = stats.geometryPass.endMs();

    lights.uploadToGpu();

    // ---- Pass 2：全屏光照（或 G-Buffer 调试）到 Swapchain ----
    VkClearValue swapClears[2]{};
    swapClears[0].color = {{0.08f, 0.09f, 0.12f, 1.0f}};
    swapClears[1].depthStencil = {1.0f, 0};
    VkRenderPassBeginInfo swapBegin{};
    swapBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    swapBegin.renderPass = swapchainRenderPass_;
    swapBegin.framebuffer = swapchainFb;
    swapBegin.renderArea.extent = extent;
    swapBegin.clearValueCount = 2;
    swapBegin.pClearValues = swapClears;
    vkCmdBeginRenderPass(cmd, &swapBegin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    if (showGBufferDebug) {
        DebugUboData debugUbo{1};
        std::memcpy(debugUboMapped_, &debugUbo, sizeof(debugUbo));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, debugPipeline_);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, debugLayout_, 0, 1, &debugSet_, 0, nullptr);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        stats.frameStats().lightingPassMs = 0.0f;
    } else {
        stats.lightingPass.begin();
        LightingUboData lightUbo{};
        lightUbo.cameraPos = glm::vec4(camera.eye, 1.0f);
        lightUbo.invView = glm::inverse(camera.view);
        lightUbo.invProj = glm::inverse(camera.projection);
        lightUbo.lightCount = lights.activeCount();
        lightUbo.enableHDR = enableHDR ? 1 : 0;
        std::memcpy(lightingUboMapped_, &lightUbo, sizeof(lightUbo));

        VkDescriptorSet lightSets[] = {lightingSet_, lightingLightSet_};
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, lightingPipeline_);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, lightingLayout_, 0, 2, lightSets, 0, nullptr);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        stats.frameStats().lightingPassMs = stats.lightingPass.endMs();
    }

    vkCmdEndRenderPass(cmd);
}
