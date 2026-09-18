// =============================================================================
// ForwardRenderer — 单 Pass 前向渲染
// =============================================================================
//
// 【录制顺序（每个 Forward 帧）】
//   1. uploadToGpu() + memcpy 片元 UBO（相机、材质、灯数）
//   2. memcpy mesh UBO（view/proj）
//   3. vkCmdBeginRenderPass(swapchain)
//   4. bind pipeline + 2 个 descriptor set (mesh + lights)
//   5. drawFloor + drawSpotMeshes × 12
//   6. vkCmdEndRenderPass
//
// 【片元代价】 forward.frag 对每个像素循环 lightCount 次（最多 512）
//   灯数多时，这是性能瓶颈；可对比 Deferred / Forward+ 模式。
//
// 【PipelineLayout】
//   set0 = meshSet_   (UBO + 纹理 + 片元 UBO)
//   set1 = lightSet_  (点光源 SSBO)
//   push constant     (mat4 model, 顶点阶段)
//
// =============================================================================

#include "ForwardRenderer.hpp"
#include "AppConfig.hpp"
#include "VulkanBuffer.hpp"
#include "VulkanRhi.hpp"

#include <glm/glm.hpp>
#include <array>
#include <cstring>

namespace {
struct MeshUboData {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
};

struct ForwardFragUboData {
    glm::vec4 cameraPos;
    glm::vec4 materialK;
    int lightCount;
    int enableHDR;
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

bool ForwardRenderer::init(VulkanContext &ctx, VkRenderPass swapchainRenderPass) {
    ctx_ = &ctx;
    swapchainRenderPass_ = swapchainRenderPass;

    meshSetLayout_ = VulkanRhi::createMeshDescriptorLayout(ctx);
    lightSsboLayout_ = VulkanRhi::createLightSsboLayout(ctx);

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushRange.size = sizeof(glm::mat4);

    VkDescriptorSetLayout layouts[] = {meshSetLayout_, lightSsboLayout_};
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 2;
    layoutInfo.pSetLayouts = layouts;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    vkCreatePipelineLayout(ctx.device(), &layoutInfo, nullptr, &layout_);

    descriptorPool_ = VulkanRhi::createDescriptorPool(ctx, 4);

    VulkanUtil::createBuffer(ctx, 128, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, meshUbo_,
                               meshUboMem_);
    vkMapMemory(ctx.device(), meshUboMem_, 0, 128, 0, &meshUboMapped_);

    VulkanUtil::createBuffer(ctx, 64, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, fragUbo_,
                               fragUboMem_);
    vkMapMemory(ctx.device(), fragUboMem_, 0, 64, 0, &fragUboMapped_);

    meshSet_ = VulkanRhi::allocateSet(ctx, descriptorPool_, meshSetLayout_);
    lightSet_ = VulkanRhi::allocateSet(ctx, descriptorPool_, lightSsboLayout_);

    auto vert = VulkanRhi::loadSpirv(ctx, AppConfig::shaderPath("mesh_vert.spv"));
    auto frag = VulkanRhi::loadSpirv(ctx, AppConfig::shaderPath("forward_frag.spv"));

    const auto bindings = meshBindings();
    const auto attrs = meshAttributes();
    VulkanRhi::PipelineInfo info{};
    info.vert = vert;
    info.frag = frag;
    info.renderPass = swapchainRenderPass_;
    info.extent = {AppConfig::kInitialWidth, AppConfig::kInitialHeight};
    info.bindings = bindings.data();
    info.bindingCount = 1;
    info.attributes = attrs.data();
    info.attributeCount = 3;
    info.layout = layout_;
    info.depthTest = true;
    info.cullMode = VK_CULL_MODE_NONE;
    pipeline_ = VulkanRhi::createGraphicsPipeline(ctx, info);

    vkDestroyShaderModule(ctx.device(), vert, nullptr);
    vkDestroyShaderModule(ctx.device(), frag, nullptr);
    return true;
}

// 仅在 init / recreateSwapchain 后调用；绑定纹理、UBO、SSBO 指针。
// 运行中改相机/灯数：render() 内 memcpy 到 mapped UBO，勿 vkUpdateDescriptorSets。
void ForwardRenderer::bindResources(const Scene &scene, LightManager &lights) {
    VulkanRhi::writeMeshDescriptor(*ctx_, meshSet_, meshUbo_, scene.whiteTextureView(), scene.sampler());
    VulkanRhi::writeLightSsboDescriptor(*ctx_, lightSet_, lights.lightBuffer());

    VkDescriptorBufferInfo fragInfo{fragUbo_, 0, VK_WHOLE_SIZE};
    VkWriteDescriptorSet fragWrite{};
    fragWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    fragWrite.dstSet = meshSet_;
    fragWrite.dstBinding = 2;
    fragWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    fragWrite.descriptorCount = 1;
    fragWrite.pBufferInfo = &fragInfo;
    vkUpdateDescriptorSets(ctx_->device(), 1, &fragWrite, 0, nullptr);
}

void ForwardRenderer::shutdown() {
    if (!ctx_) {
        return;
    }
    if (pipeline_) {
        vkDestroyPipeline(ctx_->device(), pipeline_, nullptr);
    }
    if (layout_) {
        vkDestroyPipelineLayout(ctx_->device(), layout_, nullptr);
    }
    if (descriptorPool_) {
        vkDestroyDescriptorPool(ctx_->device(), descriptorPool_, nullptr);
    }
    if (meshSetLayout_) {
        vkDestroyDescriptorSetLayout(ctx_->device(), meshSetLayout_, nullptr);
    }
    if (lightSsboLayout_) {
        vkDestroyDescriptorSetLayout(ctx_->device(), lightSsboLayout_, nullptr);
    }
    if (meshUboMapped_) {
        vkUnmapMemory(ctx_->device(), meshUboMem_);
        meshUboMapped_ = nullptr;
    }
    if (fragUboMapped_) {
        vkUnmapMemory(ctx_->device(), fragUboMem_);
        fragUboMapped_ = nullptr;
    }
    if (meshUbo_) {
        vkDestroyBuffer(ctx_->device(), meshUbo_, nullptr);
        vkFreeMemory(ctx_->device(), meshUboMem_, nullptr);
    }
    if (fragUbo_) {
        vkDestroyBuffer(ctx_->device(), fragUbo_, nullptr);
        vkFreeMemory(ctx_->device(), fragUboMem_, nullptr);
    }
    ctx_ = nullptr;
}

// 单 Pass：BeginRenderPass(swapchain) → 绘制场景 → EndRenderPass
// 片元着色器对每个可见像素循环 activeCount_ 盏灯（O(像素×灯数)）
void ForwardRenderer::render(VkCommandBuffer cmd, const Scene &scene, LightManager &lights, const FrameCamera &camera,
                             VkFramebuffer framebuffer, VkExtent2D extent, PerfStats &stats, bool enableHDR) {
    stats.forwardPass.begin();

    lights.uploadToGpu();

    ForwardFragUboData fragUbo{};
    fragUbo.cameraPos = glm::vec4(camera.eye, 1.0f);
    fragUbo.materialK = glm::vec4(AppConfig::materialCoeffs(), 1.0f);
    fragUbo.lightCount = lights.activeCount();
    fragUbo.enableHDR = enableHDR ? 1 : 0;
    std::memcpy(fragUboMapped_, &fragUbo, sizeof(fragUbo));

    scene.writeCameraUbo(meshUboMapped_, camera.view, camera.projection);

    VkClearValue clears[2]{};
    clears[0].color = {{0.08f, 0.09f, 0.12f, 1.0f}};
    clears[1].depthStencil = {1.0f, 0};
    VkRenderPassBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    beginInfo.renderPass = swapchainRenderPass_;
    beginInfo.framebuffer = framebuffer;
    beginInfo.renderArea.extent = extent;
    beginInfo.clearValueCount = 2;
    beginInfo.pClearValues = clears;
    vkCmdBeginRenderPass(cmd, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{0, 0, static_cast<float>(extent.width), static_cast<float>(extent.height), 0, 1};
    VkRect2D scissor{{0, 0}, extent};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    VkDescriptorSet sets[] = {meshSet_, lightSet_};
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 2, sets, 0, nullptr);
    scene.drawFloor(cmd, layout_, pipeline_, meshSet_);
    scene.drawSpotMeshes(cmd, layout_, pipeline_, meshSet_);

    vkCmdEndRenderPass(cmd);
    stats.frameStats().forwardPassMs = stats.forwardPass.endMs();
}
