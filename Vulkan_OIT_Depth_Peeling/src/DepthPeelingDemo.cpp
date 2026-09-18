// =============================================================================
// DepthPeelingDemo 实现
// =============================================================================
//
// 头文件 DepthPeelingDemo.hpp 含完整三阶段流程说明。
// 本文件实现：离屏 FBO 创建、三条管线、每帧 Peel/Blend/Composite 命令录制。
//
// 【与 OpenGL 的对应关系】
//   fboAccum_.color     → accumColor_
//   fboPeel_.color      → peelColor_
//   depthTexture(0/1)   → depthPing_[0/1]
//   glBlendFuncSeparate → DemoRhi::oitFrontToBackBlend（DST_ALPHA, ONE）
//
// =============================================================================

#include "DepthPeelingDemo.hpp"

#include "DemoRhi.hpp"
#include "VulkanBuffer.hpp"

#include <GLFW/glfw3.h>
#include <cstring>
#include <stdexcept>

namespace {

// Peel 顶点着色器 UBO：view / proj（model 走 PushConstant，每 draw 不同）
struct PeelVertexUbo {
    glm::mat4 view;
    glm::mat4 proj;
};

// Peel 片元着色器 UBO：Blinn-Phong 光照 + 屏幕尺寸（深度纹理 UV 换算）
struct PeelFragUbo {
    glm::vec4 cameraPos;
    glm::vec4 lightPos;
    glm::vec4 k; // x=环境光, y=漫反射, z=高光
    glm::vec4 screenSize;
};

struct CompositeUboData {
    glm::vec4 backgroundColor;
};

// 与 OitScene::Vertex / Scene::Vertex 布局一致：pos + normal + uv
struct LitVertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 uv;
};

VkVertexInputBindingDescription litVertexBinding() {
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(LitVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return binding;
}

VkVertexInputAttributeDescription litVertexAttributes[3] = {
    {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(LitVertex, pos)},
    {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(LitVertex, normal)},
    {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(LitVertex, uv)},
};

// Peel 描述符布局：与 depth_peel.vert/frag 的 binding 编号一一对应
VkDescriptorSetLayout createPeelDescriptorLayout(VulkanContext &ctx) {
    VkDescriptorSetLayoutBinding bindings[4]{};
    bindings[0] = {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr};           // view/proj
    bindings[1] = {1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};        // 光照
    bindings[2] = {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}; // diffuse
    bindings[3] = {3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}; // inputDepth

    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = 4;
    layoutInfo.pBindings = bindings;

    VkDescriptorSetLayout layout;
    if (vkCreateDescriptorSetLayout(ctx.device(), &layoutInfo, nullptr, &layout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create peel descriptor layout");
    }
    return layout;
}

// 全屏 Pass 描述符：Blend 只需 1 张层纹理；Composite 额外需要背景色 UBO
// composite=false → Blend Pass 仅采样 peelColor_；composite=true → 额外绑定背景色 UBO
VkDescriptorSetLayout createFullscreenDescriptorLayout(VulkanContext &ctx, bool composite) {
    VkDescriptorSetLayoutBinding bindings[2]{};
    bindings[0] = {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    if (composite) {
        bindings[1] = {1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = composite ? 2u : 1u;
    layoutInfo.pBindings = bindings;

    VkDescriptorSetLayout layout;
    vkCreateDescriptorSetLayout(ctx.device(), &layoutInfo, nullptr, &layout);
    return layout;
}

void writePeelDescriptor(VulkanContext &ctx, VkDescriptorSet set, VkBuffer vertexUbo, VkBuffer fragUbo,
                           VkImageView diffuseView, VkImageView depthView, VkSampler colorSampler,
                           VkSampler depthSampler) {
    VkDescriptorBufferInfo vertexInfo{vertexUbo, 0, VK_WHOLE_SIZE};
    VkDescriptorBufferInfo fragInfo{fragUbo, 0, VK_WHOLE_SIZE};
    VkDescriptorImageInfo diffuseInfo{colorSampler, diffuseView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorImageInfo depthInfo{depthSampler, depthView, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};

    VkWriteDescriptorSet writes[4]{};
    writes[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 0, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                 nullptr, &vertexInfo, nullptr};
    writes[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 1, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                 nullptr, &fragInfo, nullptr};
    writes[2] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 2, 0, 1,
                 VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &diffuseInfo, nullptr, nullptr};
    writes[3] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 3, 0, 1,
                 VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &depthInfo, nullptr, nullptr};
    vkUpdateDescriptorSets(ctx.device(), 4, writes, 0, nullptr);
}

// Peel 离屏 RenderPass：每层开始时 CLEAR 颜色(0,0,0,0) 与深度(1.0)，结束后均可被采样
// Peel Pass RP：颜色+深度均 CLEAR；结束后布局供 Blend/下一层 Peel 采样
// Peel Pass：颜色+深度均 CLEAR；结束后布局可供 Fragment Shader 采样
VkRenderPass createPeelOffscreenRenderPass(VulkanContext &ctx, VkFormat colorFormat, VkFormat depthFormat) {
    VkAttachmentDescription attachments[2]{};
    attachments[0].format = colorFormat;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    attachments[1].format = depthFormat;
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpInfo{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rpInfo.attachmentCount = 2;
    rpInfo.pAttachments = attachments;
    rpInfo.subpassCount = 1;
    rpInfo.pSubpasses = &subpass;
    rpInfo.dependencyCount = 1;
    rpInfo.pDependencies = &dep;

    VkRenderPass renderPass;
    vkCreateRenderPass(ctx.device(), &rpInfo, nullptr, &renderPass);
    return renderPass;
}

// Blend Pass RP：LOAD 保留 accum 已有内容，在其上做 Front-to-Back 混合
// Blend Pass：LOAD 保留 accum 已有内容，在其上执行 Front-to-Back 混合
VkRenderPass createAccumBlendRenderPass(VulkanContext &ctx, VkFormat colorFormat) {
    VkAttachmentDescription color{};
    color.format = colorFormat;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; // 不清除，叠加到已有累积色    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    color.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;

    VkRenderPassCreateInfo rpInfo{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rpInfo.attachmentCount = 1;
    rpInfo.pAttachments = &color;
    rpInfo.subpassCount = 1;
    rpInfo.pSubpasses = &subpass;

    VkRenderPass renderPass;
    vkCreateRenderPass(ctx.device(), &rpInfo, nullptr, &renderPass);
    return renderPass;
}

void destroyColorAttachment(VulkanContext &ctx, VkImage &image, VkDeviceMemory &memory, VkImageView &view) {
    if (view) {
        vkDestroyImageView(ctx.device(), view, nullptr);
        view = VK_NULL_HANDLE;
    }
    if (image) {
        vkDestroyImage(ctx.device(), image, nullptr);
        vkFreeMemory(ctx.device(), memory, nullptr);
        image = VK_NULL_HANDLE;
    }
}
} // namespace

DepthPeelingDemo::DepthPeelingDemo(DemoApp &app) : app_(app) {
    camera_.orbitAngleDeg = AppConfig::kInitialOrbitAngle;
    camera_.orbitRadius = AppConfig::kCameraOrbitRadius;
}

DepthPeelingDemo::~DepthPeelingDemo() { shutdown(); }

void DepthPeelingDemo::init() {
    auto &ctx = app_.ctx();
    if (!scene_.init(ctx)) {
        throw std::runtime_error("Failed to initialize OIT scene (check resources/models)");
    }

    // 三个 UBO：Peel 顶点/片元 + Composite 背景色
    VulkanUtil::createBuffer(ctx, sizeof(PeelVertexUbo), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, vertexUbo_,
                             vertexUboMemory_);
    VulkanUtil::createBuffer(ctx, sizeof(PeelFragUbo), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, fragUbo_,
                             fragUboMemory_);
    VulkanUtil::createBuffer(ctx, sizeof(CompositeUboData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, compositeUbo_,
                             compositeUboMemory_);

    peelLayout_ = createPeelDescriptorLayout(ctx);
    blendOnlyLayout_ = createFullscreenDescriptorLayout(ctx, false);
    compositeLayout_ = createFullscreenDescriptorLayout(ctx, true);
    pool_ = DemoRhi::createDescriptorPool(ctx, static_cast<uint32_t>(scene_.objects().size() + 4));

    VkSamplerCreateInfo colorSamplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    colorSamplerInfo.magFilter = VK_FILTER_LINEAR;
    colorSamplerInfo.minFilter = VK_FILTER_LINEAR;
    colorSamplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    colorSamplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    vkCreateSampler(ctx.device(), &colorSamplerInfo, nullptr, &colorSampler_);

    // 深度必须用 NEAREST，避免相邻像素深度插值导致剥离条件错误
    VkSamplerCreateInfo depthSamplerInfo{colorSamplerInfo};
    depthSamplerInfo.magFilter = VK_FILTER_NEAREST;
    depthSamplerInfo.minFilter = VK_FILTER_NEAREST;
    vkCreateSampler(ctx.device(), &depthSamplerInfo, nullptr, &depthSampler_);

    objectSets_.resize(scene_.objects().size());
    for (size_t i = 0; i < scene_.objects().size(); ++i) {
        objectSets_[i] = DemoRhi::allocateSet(ctx, pool_, peelLayout_);
    }
    blendSet_ = DemoRhi::allocateSet(ctx, pool_, blendOnlyLayout_);
    compositeSet_ = DemoRhi::allocateSet(ctx, pool_, compositeLayout_);

    peelVert_ = DemoRhi::loadSpirv(ctx, "resources/shaders/depth_peel_vert.spv");
    peelFrag_ = DemoRhi::loadSpirv(ctx, "resources/shaders/depth_peel_frag.spv");
    blendVert_ = DemoRhi::loadSpirv(ctx, "resources/shaders/blend_vert.spv");
    blendFrag_ = DemoRhi::loadSpirv(ctx, "resources/shaders/blend_frag.spv");
    compositeFrag_ = DemoRhi::loadSpirv(ctx, "resources/shaders/composite_frag.spv");

    // 每物体 model 矩阵不同 → Push Constant（避免每 draw 更新 Descriptor）
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushRange.size = sizeof(glm::mat4);

    VkPipelineLayoutCreateInfo peelLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    peelLayoutInfo.setLayoutCount = 1;
    peelLayoutInfo.pSetLayouts = &peelLayout_;
    peelLayoutInfo.pushConstantRangeCount = 1;
    peelLayoutInfo.pPushConstantRanges = &pushRange;
    vkCreatePipelineLayout(ctx.device(), &peelLayoutInfo, nullptr, &peelLayoutHandle_);

    VkPipelineLayoutCreateInfo blendLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    blendLayoutInfo.setLayoutCount = 1;
    blendLayoutInfo.pSetLayouts = &blendOnlyLayout_;
    vkCreatePipelineLayout(ctx.device(), &blendLayoutInfo, nullptr, &blendPipelineLayout_);

    VkPipelineLayoutCreateInfo compositeLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    compositeLayoutInfo.setLayoutCount = 1;
    compositeLayoutInfo.pSetLayouts = &compositeLayout_;
    vkCreatePipelineLayout(ctx.device(), &compositeLayoutInfo, nullptr, &compositePipelineLayout_);

    createOffscreenTargets();
    createPipelines();
    updateDescriptors();

    glfwSetWindowTitle(ctx.window(), "LearnOpenGL | Vulkan Depth Peeling OIT");
}

void DepthPeelingDemo::shutdown() {
    auto &ctx = app_.ctx();
    vkDeviceWaitIdle(ctx.device());
    destroyPipelines();
    destroyOffscreenTargets();
    scene_.shutdown();

    if (compositePipeline_) {
        vkDestroyPipeline(ctx.device(), compositePipeline_, nullptr);
        compositePipeline_ = VK_NULL_HANDLE;
    }
    if (blendPipeline_) {
        vkDestroyPipeline(ctx.device(), blendPipeline_, nullptr);
        blendPipeline_ = VK_NULL_HANDLE;
    }
    if (peelPipeline_) {
        vkDestroyPipeline(ctx.device(), peelPipeline_, nullptr);
        peelPipeline_ = VK_NULL_HANDLE;
    }
    if (compositePipelineLayout_) {
        vkDestroyPipelineLayout(ctx.device(), compositePipelineLayout_, nullptr);
        compositePipelineLayout_ = VK_NULL_HANDLE;
    }
    if (blendPipelineLayout_) {
        vkDestroyPipelineLayout(ctx.device(), blendPipelineLayout_, nullptr);
        blendPipelineLayout_ = VK_NULL_HANDLE;
    }
    if (peelLayoutHandle_) {
        vkDestroyPipelineLayout(ctx.device(), peelLayoutHandle_, nullptr);
        peelLayoutHandle_ = VK_NULL_HANDLE;
    }
    for (VkShaderModule *mod :
         {&compositeFrag_, &blendFrag_, &blendVert_, &peelFrag_, &peelVert_}) {
        if (*mod) {
            vkDestroyShaderModule(ctx.device(), *mod, nullptr);
            *mod = VK_NULL_HANDLE;
        }
    }
    if (depthSampler_) {
        vkDestroySampler(ctx.device(), depthSampler_, nullptr);
        depthSampler_ = VK_NULL_HANDLE;
    }
    if (colorSampler_) {
        vkDestroySampler(ctx.device(), colorSampler_, nullptr);
        colorSampler_ = VK_NULL_HANDLE;
    }
    if (pool_) {
        vkDestroyDescriptorPool(ctx.device(), pool_, nullptr);
        pool_ = VK_NULL_HANDLE;
    }
    if (compositeLayout_) {
        vkDestroyDescriptorSetLayout(ctx.device(), compositeLayout_, nullptr);
        compositeLayout_ = VK_NULL_HANDLE;
    }
    if (blendOnlyLayout_) {
        vkDestroyDescriptorSetLayout(ctx.device(), blendOnlyLayout_, nullptr);
        blendOnlyLayout_ = VK_NULL_HANDLE;
    }
    if (peelLayout_) {
        vkDestroyDescriptorSetLayout(ctx.device(), peelLayout_, nullptr);
        peelLayout_ = VK_NULL_HANDLE;
    }
    if (compositeUbo_) {
        vkDestroyBuffer(ctx.device(), compositeUbo_, nullptr);
        vkFreeMemory(ctx.device(), compositeUboMemory_, nullptr);
        compositeUbo_ = VK_NULL_HANDLE;
    }
    if (fragUbo_) {
        vkDestroyBuffer(ctx.device(), fragUbo_, nullptr);
        vkFreeMemory(ctx.device(), fragUboMemory_, nullptr);
        fragUbo_ = VK_NULL_HANDLE;
    }
    if (vertexUbo_) {
        vkDestroyBuffer(ctx.device(), vertexUbo_, nullptr);
        vkFreeMemory(ctx.device(), vertexUboMemory_, nullptr);
        vertexUbo_ = VK_NULL_HANDLE;
    }
    objectSets_.clear();
}

void DepthPeelingDemo::onResize() {
    // 离屏附件尺寸跟随 Swapchain，需完整重建 RP / FB / Pipeline
    camera_.setAspectFromViewport(app_.extent().width, app_.extent().height);
    destroyPipelines();
    destroyOffscreenTargets();
    createOffscreenTargets();
    createPipelines();
    updateDescriptors();
}

void DepthPeelingDemo::onKey(int key, int action) {
    if (action != GLFW_PRESS && action != GLFW_REPEAT) {
        return;
    }
    if (key == GLFW_KEY_LEFT) {
        camera_.orbitAngleDeg += 1.0f;
    } else if (key == GLFW_KEY_RIGHT) {
        camera_.orbitAngleDeg -= 1.0f;
    }
}

void DepthPeelingDemo::onMouseButton(int button, int action, double x, double y) {
    camera_.onMouseButton(button, action, x, y);
}

void DepthPeelingDemo::onCursorPos(double x, double y, int width, int height) {
    camera_.onCursorPos(x, y, width, height);
}

void DepthPeelingDemo::onScroll(double yoffset) {
    camera_.onScroll(yoffset);
}

// 创建 accum / peel 颜色附件、乒乓深度、对应 Framebuffer 与 RenderPass
void DepthPeelingDemo::createOffscreenTargets() {
    auto &ctx = app_.ctx();
    const auto extent = ctx.swapChainExtent();
    const VkFormat colorFormat = VK_FORMAT_R16G16B16A16_SFLOAT; // HDR 累积，避免多层混合精度损失
    const VkFormat depthFormat = VulkanUtil::findDepthFormat(ctx);

    auto createColor = [&](ColorAttachment &target) {
        VulkanUtil::createImage(ctx, extent.width, extent.height, 1, VK_SAMPLE_COUNT_1_BIT, colorFormat,
                                VK_IMAGE_TILING_OPTIMAL,
                                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, target.image, target.memory);
        target.view = VulkanUtil::createImageView(ctx, target.image, colorFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);
    };
    createColor(accumColor_);
    createColor(peelColor_);

    // 乒乓深度：TRANSFER_DST 用于帧初 glClearDepth(0) 等价操作
    for (int i = 0; i < 2; ++i) {
        VulkanUtil::createImage(ctx, extent.width, extent.height, 1, VK_SAMPLE_COUNT_1_BIT, depthFormat,
                                VK_IMAGE_TILING_OPTIMAL,
                                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                    VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, depthPing_[i], depthPingMem_[i]);
        depthPingView_[i] =
            VulkanUtil::createImageView(ctx, depthPing_[i], depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT, 1);
    }

    peelRenderPass_ = createPeelOffscreenRenderPass(ctx, colorFormat, depthFormat);
    accumBlendRenderPass_ = createAccumBlendRenderPass(ctx, colorFormat);

    VkImageView accumViews[] = {accumColor_.view};
    VkFramebufferCreateInfo accumFb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    accumFb.renderPass = accumBlendRenderPass_;
    accumFb.attachmentCount = 1;
    accumFb.pAttachments = accumViews;
    accumFb.width = extent.width;
    accumFb.height = extent.height;
    accumFb.layers = 1;
    vkCreateFramebuffer(ctx.device(), &accumFb, nullptr, &accumFramebuffer_);

    for (int i = 0; i < 2; ++i) {
        VkImageView attachments[] = {peelColor_.view, depthPingView_[i]};
        VkFramebufferCreateInfo peelFb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        peelFb.renderPass = peelRenderPass_;
        peelFb.attachmentCount = 2;
        peelFb.pAttachments = attachments;
        peelFb.width = extent.width;
        peelFb.height = extent.height;
        peelFb.layers = 1;
        vkCreateFramebuffer(ctx.device(), &peelFb, nullptr, &peelFramebuffer_[i]);
    }
}

void DepthPeelingDemo::destroyOffscreenTargets() {
    auto &ctx = app_.ctx();
    if (accumFramebuffer_) {
        vkDestroyFramebuffer(ctx.device(), accumFramebuffer_, nullptr);
        accumFramebuffer_ = VK_NULL_HANDLE;
    }
    for (int i = 0; i < 2; ++i) {
        if (peelFramebuffer_[i]) {
            vkDestroyFramebuffer(ctx.device(), peelFramebuffer_[i], nullptr);
            peelFramebuffer_[i] = VK_NULL_HANDLE;
        }
        if (depthPingView_[i]) {
            vkDestroyImageView(ctx.device(), depthPingView_[i], nullptr);
        }
        if (depthPing_[i]) {
            vkDestroyImage(ctx.device(), depthPing_[i], nullptr);
            vkFreeMemory(ctx.device(), depthPingMem_[i], nullptr);
        }
        depthPingView_[i] = VK_NULL_HANDLE;
        depthPing_[i] = VK_NULL_HANDLE;
    }
    destroyColorAttachment(ctx, accumColor_.image, accumColor_.memory, accumColor_.view);
    destroyColorAttachment(ctx, peelColor_.image, peelColor_.memory, peelColor_.view);
    if (accumBlendRenderPass_) {
        vkDestroyRenderPass(ctx.device(), accumBlendRenderPass_, nullptr);
        accumBlendRenderPass_ = VK_NULL_HANDLE;
    }
    if (peelRenderPass_) {
        vkDestroyRenderPass(ctx.device(), peelRenderPass_, nullptr);
        peelRenderPass_ = VK_NULL_HANDLE;
    }
}

void DepthPeelingDemo::createPipelines() {
    auto &ctx = app_.ctx();
    const auto extent = ctx.swapChainExtent();
    auto binding = litVertexBinding();

    // Peel：深度测试 LESS + 写深度；双面绘制（与 OpenGL 一致）
    DemoRhi::PipelineInfo peelInfo{};
    peelInfo.vert = peelVert_;
    peelInfo.frag = peelFrag_;
    peelInfo.renderPass = peelRenderPass_;
    peelInfo.extent = extent;
    peelInfo.bindings = &binding;
    peelInfo.bindingCount = 1;
    peelInfo.attributes = litVertexAttributes;
    peelInfo.attributeCount = 3;
    peelInfo.layout = peelLayoutHandle_;
    peelInfo.depthTest = true;
    peelInfo.cullMode = VK_CULL_MODE_NONE;
    peelPipeline_ = DemoRhi::createGraphicsPipeline(ctx, peelInfo);

    // Blend：全屏三角，无深度，OIT 前向混合
    DemoRhi::PipelineInfo blendInfo{};
    blendInfo.vert = blendVert_;
    blendInfo.frag = blendFrag_;
    blendInfo.renderPass = accumBlendRenderPass_;
    blendInfo.extent = extent;
    blendInfo.layout = blendPipelineLayout_;
    blendInfo.depthTest = false;
    blendInfo.oitFrontToBackBlend = true;
    blendPipeline_ = DemoRhi::createGraphicsPipeline(ctx, blendInfo);

    // Composite：accum + 背景色 → Swapchain（使用 DemoApp 的 swapRenderPass）
    // Composite：绘制到 DemoApp Swapchain FB
    DemoRhi::PipelineInfo compositeInfo{};
    compositeInfo.vert = blendVert_;
    compositeInfo.frag = compositeFrag_;
    compositeInfo.renderPass = app_.swapRenderPass();
    compositeInfo.extent = extent;
    compositeInfo.layout = compositePipelineLayout_;
    compositeInfo.depthTest = false;
    compositePipeline_ = DemoRhi::createGraphicsPipeline(ctx, compositeInfo);
}

void DepthPeelingDemo::destroyPipelines() {
    auto &ctx = app_.ctx();
    if (compositePipeline_) {
        vkDestroyPipeline(ctx.device(), compositePipeline_, nullptr);
        compositePipeline_ = VK_NULL_HANDLE;
    }
    if (blendPipeline_) {
        vkDestroyPipeline(ctx.device(), blendPipeline_, nullptr);
        blendPipeline_ = VK_NULL_HANDLE;
    }
    if (peelPipeline_) {
        vkDestroyPipeline(ctx.device(), peelPipeline_, nullptr);
        peelPipeline_ = VK_NULL_HANDLE;
    }
}

void DepthPeelingDemo::updateDescriptors() {
    auto &ctx = app_.ctx();
    const auto &objects = scene_.objects();
    for (size_t i = 0; i < objects.size(); ++i) {
        writePeelDescriptor(ctx, objectSets_[i], vertexUbo_, fragUbo_, objects[i].diffuseView,
                              depthPingView_[inputDepthIndex_], colorSampler_, depthSampler_);
    }

    VkDescriptorImageInfo peelLayerInfo{colorSampler_, peelColor_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet blendWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, blendSet_, 0, 0, 1,
                                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &peelLayerInfo, nullptr, nullptr};
    vkUpdateDescriptorSets(ctx.device(), 1, &blendWrite, 0, nullptr);

    VkDescriptorImageInfo accumInfo{colorSampler_, accumColor_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorBufferInfo compositeInfo{compositeUbo_, 0, VK_WHOLE_SIZE};
    VkWriteDescriptorSet compositeWrites[2]{};
    compositeWrites[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, compositeSet_, 0, 0, 1,
                          VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &accumInfo, nullptr, nullptr};
    compositeWrites[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, compositeSet_, 1, 0, 1,
                          VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &compositeInfo, nullptr};
    vkUpdateDescriptorSets(ctx.device(), 2, compositeWrites, 0, nullptr);
}

// 帧初将 accum 清为 (0,0,0,1)：对应 OpenGL glClearColor(0,0,0,1)
// 等价 OpenGL initPeelBuffers 中 glClearColor(0,0,0,1) on fboAccum_
void DepthPeelingDemo::clearAccumColor(VkCommandBuffer cmd) {
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.image = accumColor_.image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &barrier);

    const VkClearColorValue clearValue{{0.0f, 0.0f, 0.0f, 1.0f}};
    const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdClearColorImage(cmd, accumColor_.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearValue, 1, &range);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &barrier);
}

// Peel Pass 写颜色附件 → Blend Pass 片元着色器采样，需显式同步
void DepthPeelingDemo::barrierPeelToBlend(VkCommandBuffer cmd, bool accumBlended) {
    VkImageMemoryBarrier barriers[2]{};
    barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barriers[0].image = peelColor_.image;
    barriers[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barriers[0].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barriers[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barriers[0].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    uint32_t barrierCount = 1;
    // 第一层 Blend 前 accum 仅被 transfer clear，无需 COLOR_ATTACHMENT_WRITE barrier
    if (accumBlended) {
        barriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barriers[1].image = accumColor_.image;
        barriers[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barriers[1].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barriers[1].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barriers[1].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        barrierCount = 2;
    }

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                         0, nullptr, 0, nullptr, barrierCount, barriers);
}

// 等价 OpenGL glClearDepth(0.0) — 历史深度初值，保证第一层 z > 0 的片元不被 discard
void DepthPeelingDemo::clearDepthPing(VkCommandBuffer cmd, int index, float depth) {
    (void)app_.ctx();
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.image = depthPing_[index];
    barrier.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &barrier);

    VkClearDepthStencilValue clearValue{};
    clearValue.depth = depth;
    VkImageSubresourceRange range{VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    vkCmdClearDepthStencilImage(cmd, depthPing_[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearValue, 1, &range);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &barrier);
}

void DepthPeelingDemo::drawScene(VkCommandBuffer cmd, VkPipeline pipeline, VkExtent2D extent) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    VkViewport viewport{0, 0, static_cast<float>(extent.width), static_cast<float>(extent.height), 0, 1};
    VkRect2D scissor{{0, 0}, extent};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    const auto &objects = scene_.objects();
    for (size_t i = 0; i < objects.size(); ++i) {
        const auto &obj = objects[i];
        vkCmdPushConstants(cmd, peelLayoutHandle_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &obj.model);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, peelLayoutHandle_, 0, 1, &objectSets_[i], 0,
                                nullptr);
        VkBuffer vb[] = {obj.mesh.vertexBuffer};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cmd, 0, 1, vb, offsets);
        vkCmdBindIndexBuffer(cmd, obj.mesh.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, obj.mesh.indexCount, 1, 0, 0, 0);
    }
}

// 全屏三角形（blend_vert：无 VBO，3 顶点）
void DepthPeelingDemo::drawFullscreen(VkCommandBuffer cmd, VkPipeline pipeline, VkPipelineLayout layout,
                                      VkDescriptorSet set, VkExtent2D extent) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &set, 0, nullptr);
    VkViewport viewport{0, 0, static_cast<float>(extent.width), static_cast<float>(extent.height), 0, 1};
    VkRect2D scissor{{0, 0}, extent};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

void DepthPeelingDemo::render(DemoFrame &frame) {
    const auto extent = frame.extent;
    camera_.setAspectFromViewport(extent.width, extent.height);

    // --- 更新每帧 UBO ---
    const glm::mat4 view = camera_.view();
    const glm::mat4 proj = camera_.projection();

    PeelVertexUbo vertexUbo{view, proj};
    void *mapped = nullptr;
    vkMapMemory(app_.ctx().device(), vertexUboMemory_, 0, sizeof(PeelVertexUbo), 0, &mapped);
    std::memcpy(mapped, &vertexUbo, sizeof(PeelVertexUbo));
    vkUnmapMemory(app_.ctx().device(), vertexUboMemory_);

    PeelFragUbo fragUbo{};
    fragUbo.cameraPos = glm::vec4(camera_.eye(), 1.0f);
    fragUbo.lightPos = glm::vec4(AppConfig::lightPosition(), 1.0f);
    fragUbo.k = glm::vec4(AppConfig::materialCoeffs(), 0.0f);
    fragUbo.screenSize =
        glm::vec4(static_cast<float>(extent.width), static_cast<float>(extent.height > 0 ? extent.height : 1), 0.0f, 0.0f);
    vkMapMemory(app_.ctx().device(), fragUboMemory_, 0, sizeof(PeelFragUbo), 0, &mapped);
    std::memcpy(mapped, &fragUbo, sizeof(PeelFragUbo));
    vkUnmapMemory(app_.ctx().device(), fragUboMemory_);

    const glm::vec3 bg = AppConfig::backgroundColor();
    CompositeUboData compositeUbo{glm::vec4(bg, 1.0f)};
    vkMapMemory(app_.ctx().device(), compositeUboMemory_, 0, sizeof(CompositeUboData), 0, &mapped);
    std::memcpy(mapped, &compositeUbo, sizeof(CompositeUboData));
    vkUnmapMemory(app_.ctx().device(), compositeUboMemory_);

    // --- Phase 0: initPeelBuffers ---
    inputDepthIndex_ = 0;
    outputDepthIndex_ = 1;

    clearAccumColor(frame.cmd);
    clearDepthPing(frame.cmd, 0, 0.0f);
    clearDepthPing(frame.cmd, 1, 0.0f);

    // Peel FBO 每层清空：颜色透明 + 深度 1.0（硬件 LESS 选本层最近片元）
    VkClearValue peelClears[2]{};
    peelClears[0].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
    peelClears[1].depthStencil = {1.0f, 0};

    // --- Phase 1: peelAndBlend ---
    for (int layer = 0; layer < AppConfig::kMaxDepthPeelLayers; ++layer) {
        // 每层切换 input 深度纹理（乒乓索引在循环末尾 swap）
        for (size_t i = 0; i < scene_.objects().size(); ++i) {
            writePeelDescriptor(app_.ctx(), objectSets_[i], vertexUbo_, fragUbo_, scene_.objects()[i].diffuseView,
                                  depthPingView_[inputDepthIndex_], colorSampler_, depthSampler_);
        }

        // A. Peel Pass
        VkRenderPassBeginInfo peelBegin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        peelBegin.renderPass = peelRenderPass_;
        peelBegin.framebuffer = peelFramebuffer_[outputDepthIndex_];
        peelBegin.renderArea.extent = extent;
        peelBegin.clearValueCount = 2;
        peelBegin.pClearValues = peelClears;
        vkCmdBeginRenderPass(frame.cmd, &peelBegin, VK_SUBPASS_CONTENTS_INLINE);
        drawScene(frame.cmd, peelPipeline_, extent);
        vkCmdEndRenderPass(frame.cmd);

        barrierPeelToBlend(frame.cmd, layer > 0);

        // B. Blend Pass — 将 peelColor_ 并入 accumColor_
        VkRenderPassBeginInfo blendBegin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        blendBegin.renderPass = accumBlendRenderPass_;
        blendBegin.framebuffer = accumFramebuffer_;
        blendBegin.renderArea.extent = extent;
        vkCmdBeginRenderPass(frame.cmd, &blendBegin, VK_SUBPASS_CONTENTS_INLINE);
        drawFullscreen(frame.cmd, blendPipeline_, blendPipelineLayout_, blendSet_, extent);
        vkCmdEndRenderPass(frame.cmd);

        // C. 乒乓交换深度读写索引
        std::swap(inputDepthIndex_, outputDepthIndex_);
    }

    // --- Phase 2: compositeToScreen ---
    const glm::vec3 bgColor = AppConfig::backgroundColor();
    VkClearValue swapClears[2]{};
    swapClears[0].color = {{bgColor.r, bgColor.g, bgColor.b, 1.0f}};
    swapClears[1].depthStencil = {1.0f, 0};
    {
        VkImageMemoryBarrier accumBarrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        accumBarrier.image = accumColor_.image;
        accumBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        accumBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        accumBarrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        accumBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        accumBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(frame.cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &accumBarrier);
    }

    VkRenderPassBeginInfo compositeBegin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    compositeBegin.renderPass = app_.swapRenderPass();
    compositeBegin.framebuffer = frame.framebuffer;
    compositeBegin.renderArea.extent = extent;
    compositeBegin.clearValueCount = 2;
    compositeBegin.pClearValues = swapClears;
    vkCmdBeginRenderPass(frame.cmd, &compositeBegin, VK_SUBPASS_CONTENTS_INLINE);
    drawFullscreen(frame.cmd, compositePipeline_, compositePipelineLayout_, compositeSet_, extent);
    vkCmdEndRenderPass(frame.cmd);
}
