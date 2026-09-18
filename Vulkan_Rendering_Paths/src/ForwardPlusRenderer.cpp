// =============================================================================
// ForwardPlusRenderer — Tile-Based Light Culling + 前向着色
// =============================================================================
//
// 【与 Forward 的区别】
//   多一步 CPU：buildForwardPlusTiles() 把每盏灯映射到 16×16 屏幕 Tile
//   片元着色器只读当前像素所属 Tile 的光源列表（最多 64 盏/Tile）
//
// 【SSBO 布局 set1】
//   binding0: PointLight[]           全部光源数据
//   binding1: uint tileLightCount[]  每 Tile 实际灯数
//   binding2: uint tileLightIndex[]   每 Tile 灯索引（扁平数组，stride=maxLightsPerTile）
//
// 【片元 UBO 额外字段】 tilesX, tilesY, tileSize, maxLightsPerTile
//   片元里用 gl_FragCoord 计算 tileId = (x/tileSize) + (y/tileSize)*tilesX
//
// =============================================================================

#include "ForwardPlusRenderer.hpp"
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

struct ForwardPlusFragUboData {
    glm::vec4 cameraPos;
    glm::vec4 materialK;
    int lightCount;
    int tilesX;
    int tilesY;
    int tileSize;
    int maxLightsPerTile;
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

bool ForwardPlusRenderer::init(VulkanContext &ctx, VkRenderPass swapchainRenderPass) {
    ctx_ = &ctx;
    swapchainRenderPass_ = swapchainRenderPass;

    meshSetLayout_ = VulkanRhi::createMeshDescriptorLayout(ctx);
    tileSsboLayout_ = VulkanRhi::createForwardPlusSsboLayout(ctx);

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushRange.size = sizeof(glm::mat4);

    VkDescriptorSetLayout layouts[] = {meshSetLayout_, tileSsboLayout_};
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
    tileSet_ = VulkanRhi::allocateSet(ctx, descriptorPool_, tileSsboLayout_);

    auto vert = VulkanRhi::loadSpirv(ctx, AppConfig::shaderPath("mesh_vert.spv"));
    auto frag = VulkanRhi::loadSpirv(ctx, AppConfig::shaderPath("forward_plus_frag.spv"));

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

void ForwardPlusRenderer::bindResources(const Scene &scene, LightManager &lights) {
    VulkanRhi::writeMeshDescriptor(*ctx_, meshSet_, meshUbo_, scene.whiteTextureView(), scene.sampler());
    VulkanRhi::writeForwardPlusSsboDescriptor(*ctx_, tileSet_, lights.lightBuffer(), lights.tileCountBuffer(),
                                             lights.tileIndexBuffer());

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

void ForwardPlusRenderer::shutdown() {
    if (!ctx_) {
        return;
    }
    if (pipeline_) {
        vkDestroyPipeline(ctx_->device(), pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }
    if (layout_) {
        vkDestroyPipelineLayout(ctx_->device(), layout_, nullptr);
        layout_ = VK_NULL_HANDLE;
    }
    if (descriptorPool_) {
        vkDestroyDescriptorPool(ctx_->device(), descriptorPool_, nullptr);
        descriptorPool_ = VK_NULL_HANDLE;
    }
    if (meshSetLayout_) {
        vkDestroyDescriptorSetLayout(ctx_->device(), meshSetLayout_, nullptr);
        meshSetLayout_ = VK_NULL_HANDLE;
    }
    if (tileSsboLayout_) {
        vkDestroyDescriptorSetLayout(ctx_->device(), tileSsboLayout_, nullptr);
        tileSsboLayout_ = VK_NULL_HANDLE;
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
        meshUbo_ = VK_NULL_HANDLE;
    }
    if (fragUbo_) {
        vkDestroyBuffer(ctx_->device(), fragUbo_, nullptr);
        vkFreeMemory(ctx_->device(), fragUboMem_, nullptr);
        fragUbo_ = VK_NULL_HANDLE;
    }
    ctx_ = nullptr;
}

void ForwardPlusRenderer::render(VkCommandBuffer cmd, const Scene &scene, LightManager &lights,
                                 const FrameCamera &camera, VkFramebuffer framebuffer, VkExtent2D extent,
                                 PerfStats &stats, bool enableHDR) {
    // CPU 阶段：构建每 Tile 的光源索引表（结果写入 HOST_VISIBLE SSBO）
    stats.cullPass.begin();
    lights.buildForwardPlusTiles(static_cast<int>(extent.width), static_cast<int>(extent.height), camera);
    stats.frameStats().cullPassMs = stats.cullPass.endMs();

    stats.shadingPass.begin();

    lights.uploadToGpu();

    ForwardPlusFragUboData fragUbo{};
    fragUbo.cameraPos = glm::vec4(camera.eye, 1.0f);
    fragUbo.materialK = glm::vec4(AppConfig::materialCoeffs(), 1.0f);
    fragUbo.lightCount = lights.activeCount();
    fragUbo.tilesX = lights.tilesX();
    fragUbo.tilesY = lights.tilesY();
    fragUbo.tileSize = AppConfig::kTileSize;
    fragUbo.maxLightsPerTile = AppConfig::kMaxLightsPerTile;
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

    VkDescriptorSet sets[] = {meshSet_, tileSet_};
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 2, sets, 0, nullptr);

    scene.drawFloor(cmd, layout_, pipeline_, meshSet_);
    scene.drawSpotMeshes(cmd, layout_, pipeline_, meshSet_);

    vkCmdEndRenderPass(cmd);
    stats.frameStats().shadingPassMs = stats.shadingPass.endMs();
}
