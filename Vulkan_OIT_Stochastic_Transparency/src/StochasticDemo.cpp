/**
 * @file StochasticDemo.cpp
 * @brief OIT Stochastic Transparency 随机透明度 Vulkan 实现
 *
 * =================================================================================
 * 【OpenGL 与 Vulkan Stochastic Transparency 架构对应关系】
 *
 * 1. 核心原理（两 API 相同）：
 *    - 单 Pass 算法：将 Alpha 透明度转换为子像素采样覆盖概率 (Subpixel Coverage)。
 *    - Fragment Shader 根据 Alpha 概率随机开启/关闭 MSAA 采样点，硬件 Resolve 求平均。
 *    - 所有透明/不透明物体均开启深度测试与深度写入，子像素级别自然遮挡，无需排序。
 *
 * 2. OpenGL 状态                     →  Vulkan 等价
 *    glEnable(GL_MULTISAMPLE)         →  RenderPass attachment samples = N
 *    glEnable(GL_SAMPLE_MASK)         →  Fragment Shader 写入 gl_SampleMask[]
 *    glEnable(GL_DEPTH_TEST)          →  Pipeline depthTestEnable = true
 *    glDepthMask(GL_TRUE)             →  Pipeline depthWriteEnable = true
 *    glfwWindowHint(GLFW_SAMPLES,16)  →  getStochasticSampleCount() 取 GPU 最高档位
 *
 * 3. MSAA RenderPass 附件布局（DemoRhi::createMsaaRenderPass）：
 *    attachment[0] 多采样颜色 (TRANSIENT, CLEAR)
 *    attachment[1] Resolve 目标 = Swapchain 图像 (STORE → PRESENT)
 *    attachment[2] 多采样深度 (CLEAR)
 *    注意：vkCmdBeginRenderPass 的 pClearValues 必须提供 3 个元素（索引 0 和 2 参与 CLEAR）。
 *
 * 4. MSAA 不可用时（sampleCnt=1）：
 *    Fragment Shader 回退到 discard 随机剔除，效果带噪声但可运行。
 * =================================================================================
 */

#include "StochasticDemo.hpp"

#include "DemoRhi.hpp"
#include "VulkanBuffer.hpp"

#include <GLFW/glfw3.h>

#include <cstring>
#include <iostream>
#include <stdexcept>

namespace {

/** 与 Assimp 加载顶点布局一致：pos + normal + uv */
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

/**
 * 查询 GPU 支持的最高 MSAA 采样数（颜色与深度 attachment 交集）。
 * 优先 16x → 8x → 4x → 2x → 1x，对应 OpenGL 请求 GLFW_SAMPLES=16 的行为。
 */
VkSampleCountFlagBits getStochasticSampleCount(VulkanContext &ctx) {
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(ctx.physicalDevice(), &props);
    const VkSampleCountFlags counts =
        props.limits.framebufferColorSampleCounts & props.limits.framebufferDepthSampleCounts;

    const VkSampleCountFlagBits candidates[] = {
        VK_SAMPLE_COUNT_16_BIT, VK_SAMPLE_COUNT_8_BIT, VK_SAMPLE_COUNT_4_BIT, VK_SAMPLE_COUNT_2_BIT,
        VK_SAMPLE_COUNT_1_BIT,
    };
    for (const auto candidate : candidates) {
        if (counts & candidate) {
            return candidate;
        }
    }
    return VK_SAMPLE_COUNT_1_BIT;
}

/** VkSampleCountFlagBits → 整数，供 Fragment Shader 循环 gl_SampleMask 使用 */
int32_t sampleCountFromFlag(VkSampleCountFlagBits samples) {
    switch (samples) {
    case VK_SAMPLE_COUNT_16_BIT:
        return 16;
    case VK_SAMPLE_COUNT_8_BIT:
        return 8;
    case VK_SAMPLE_COUNT_4_BIT:
        return 4;
    case VK_SAMPLE_COUNT_2_BIT:
        return 2;
    default:
        return 1;
    }
}

/** 写入 mesh descriptor：binding0=VertexUbo, binding1=diffuse 纹理 */
void writeMeshDescriptor(VulkanContext &ctx, VkDescriptorSet set, VkBuffer vertexUbo, VkImageView diffuseView,
                         VkSampler sampler) {
    VkDescriptorBufferInfo vertexInfo{vertexUbo, 0, VK_WHOLE_SIZE};
    VkDescriptorImageInfo texInfo{sampler, diffuseView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

    VkWriteDescriptorSet writes[2]{};
    writes[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 0, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                 nullptr, &vertexInfo, nullptr};
    writes[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 1, 0, 1,
                 VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &texInfo, nullptr, nullptr};
    vkUpdateDescriptorSets(ctx.device(), 2, writes, 0, nullptr);
}
} // namespace

StochasticDemo::StochasticDemo(DemoApp &app) : app_(app) {
    camera_.orbitAngleDeg = AppConfig::kInitialOrbitAngle;
    camera_.orbitRadius = AppConfig::kCameraOrbitRadius;
}

StochasticDemo::~StochasticDemo() { shutdown(); }

void StochasticDemo::init() {
    auto &ctx = app_.ctx();
    if (!scene_.init(ctx)) {
        throw std::runtime_error("Failed to initialize scene (check resources/models)");
    }

    msaaSamples_ = getStochasticSampleCount(ctx);
    sampleCount_ = sampleCountFromFlag(msaaSamples_);
    std::cout << "Stochastic Transparency MSAA samples: " << sampleCount_ << std::endl;

    VulkanUtil::createBuffer(ctx, sizeof(VertexUbo), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, vertexUbo_,
                             vertexUboMemory_);

    // Descriptor 布局：binding0 顶点 UBO，binding1 diffuse 纹理
    VkDescriptorSetLayoutBinding bindings[2]{};
    bindings[0] = {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr};
    bindings[1] = {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};

    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = 2;
    layoutInfo.pBindings = bindings;
    vkCreateDescriptorSetLayout(ctx.device(), &layoutInfo, nullptr, &meshLayout_);

    pool_ = DemoRhi::createDescriptorPool(ctx, static_cast<uint32_t>(scene_.objects().size() + 2));
    objectSets_.resize(scene_.objects().size());
    for (size_t i = 0; i < scene_.objects().size(); ++i) {
        objectSets_[i] = DemoRhi::allocateSet(ctx, pool_, meshLayout_);
    }

    vert_ = DemoRhi::loadSpirv(ctx, "resources/shaders/stochastic_vert.spv");
    frag_ = DemoRhi::loadSpirv(ctx, "resources/shaders/stochastic_frag.spv");

    // Push Constants：model + frameID + sampleCnt，顶点/片段阶段均可见
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.size = sizeof(PushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &meshLayout_;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushRange;
    vkCreatePipelineLayout(ctx.device(), &pipelineLayoutInfo, nullptr, &pipelineLayout_);

    createMsaaTargets();
    createPipelines();
    updateDescriptors();

    glfwSetWindowTitle(ctx.window(), AppConfig::kWindowTitle);
}

void StochasticDemo::shutdown() {
    auto &ctx = app_.ctx();
    vkDeviceWaitIdle(ctx.device());

    destroyPipelines();
    destroyMsaaTargets();
    scene_.shutdown();

    if (pipelineLayout_) {
        vkDestroyPipelineLayout(ctx.device(), pipelineLayout_, nullptr);
        pipelineLayout_ = VK_NULL_HANDLE;
    }
    if (frag_) {
        vkDestroyShaderModule(ctx.device(), frag_, nullptr);
        frag_ = VK_NULL_HANDLE;
    }
    if (vert_) {
        vkDestroyShaderModule(ctx.device(), vert_, nullptr);
        vert_ = VK_NULL_HANDLE;
    }
    if (pool_) {
        vkDestroyDescriptorPool(ctx.device(), pool_, nullptr);
        pool_ = VK_NULL_HANDLE;
    }
    if (meshLayout_) {
        vkDestroyDescriptorSetLayout(ctx.device(), meshLayout_, nullptr);
        meshLayout_ = VK_NULL_HANDLE;
    }
    if (vertexUbo_) {
        vkDestroyBuffer(ctx.device(), vertexUbo_, nullptr);
        vkFreeMemory(ctx.device(), vertexUboMemory_, nullptr);
        vertexUbo_ = VK_NULL_HANDLE;
    }
    objectSets_.clear();
}

void StochasticDemo::onResize() {
    camera_.setAspectFromViewport(app_.extent().width, app_.extent().height);
    destroyPipelines();
    destroyMsaaTargets();
    createMsaaTargets();
    createPipelines();
    updateDescriptors();
}

void StochasticDemo::onMouseButton(int button, int action, double x, double y) {
    camera_.onMouseButton(button, action, x, y);
}

void StochasticDemo::onCursorPos(double x, double y, int width, int height) {
    camera_.onCursorPos(x, y, width, height);
}

void StochasticDemo::onScroll(double yoffset) {
    camera_.onScroll(yoffset);
}

// ---------------------------------------------------------------------------
// MSAA 离屏目标：多采样颜色 + 多采样深度 + Resolve 到 Swapchain
// ---------------------------------------------------------------------------
void StochasticDemo::createMsaaTargets() {
    auto &ctx = app_.ctx();
    const auto extent = ctx.swapChainExtent();

    if (msaaSamples_ == VK_SAMPLE_COUNT_1_BIT) {
        return;
    }

    msaaRenderPass_ = DemoRhi::createMsaaRenderPass(ctx, msaaSamples_);

    VulkanUtil::createImage(ctx, extent.width, extent.height, 1, msaaSamples_, ctx.swapChainImageFormat(),
                            VK_IMAGE_TILING_OPTIMAL,
                            VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, msaaColor_, msaaColorMem_);
    msaaColorView_ =
        VulkanUtil::createImageView(ctx, msaaColor_, ctx.swapChainImageFormat(), VK_IMAGE_ASPECT_COLOR_BIT, 1);

    VulkanUtil::createImage(ctx, extent.width, extent.height, 1, msaaSamples_, VulkanUtil::findDepthFormat(ctx),
                            VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, msaaDepth_, msaaDepthMem_);
    msaaDepthView_ = VulkanUtil::createImageView(ctx, msaaDepth_, VulkanUtil::findDepthFormat(ctx),
                                                 VK_IMAGE_ASPECT_DEPTH_BIT, 1);

    // 每个 Swapchain 图像对应一个 MSAA FB（Resolve 目标不同）
    msaaFramebuffers_.resize(ctx.swapChainImageViews().size());
    for (size_t i = 0; i < ctx.swapChainImageViews().size(); ++i) {
        msaaFramebuffers_[i] = DemoRhi::createMsaaFramebuffer(ctx, msaaRenderPass_, msaaColorView_,
                                                                ctx.swapChainImageViews()[i], msaaDepthView_, extent);
    }
}

void StochasticDemo::destroyMsaaTargets() {
    auto &ctx = app_.ctx();
    for (auto fb : msaaFramebuffers_) {
        vkDestroyFramebuffer(ctx.device(), fb, nullptr);
    }
    msaaFramebuffers_.clear();

    if (msaaDepthView_) {
        vkDestroyImageView(ctx.device(), msaaDepthView_, nullptr);
        msaaDepthView_ = VK_NULL_HANDLE;
    }
    if (msaaDepth_) {
        vkDestroyImage(ctx.device(), msaaDepth_, nullptr);
        vkFreeMemory(ctx.device(), msaaDepthMem_, nullptr);
        msaaDepth_ = VK_NULL_HANDLE;
    }
    if (msaaColorView_) {
        vkDestroyImageView(ctx.device(), msaaColorView_, nullptr);
        msaaColorView_ = VK_NULL_HANDLE;
    }
    if (msaaColor_) {
        vkDestroyImage(ctx.device(), msaaColor_, nullptr);
        vkFreeMemory(ctx.device(), msaaColorMem_, nullptr);
        msaaColor_ = VK_NULL_HANDLE;
    }
    if (msaaRenderPass_) {
        vkDestroyRenderPass(ctx.device(), msaaRenderPass_, nullptr);
        msaaRenderPass_ = VK_NULL_HANDLE;
    }
}

void StochasticDemo::createPipelines() {
    auto &ctx = app_.ctx();
    const auto extent = ctx.swapChainExtent();
    auto binding = litVertexBinding();

    const bool useMsaa = msaaSamples_ != VK_SAMPLE_COUNT_1_BIT && !msaaFramebuffers_.empty();
    const VkRenderPass renderPass = useMsaa ? msaaRenderPass_ : app_.swapRenderPass();

    DemoRhi::PipelineInfo info{};
    info.vert = vert_;
    info.frag = frag_;
    info.renderPass = renderPass;
    info.extent = extent;
    info.bindings = &binding;
    info.bindingCount = 1;
    info.attributes = litVertexAttributes;
    info.attributeCount = 3;
    info.layout = pipelineLayout_;
    info.depthTest = true;  // Stochastic Transparency：透明物体也写深度
    info.cullMode = VK_CULL_MODE_NONE;
    info.samples = useMsaa ? msaaSamples_ : VK_SAMPLE_COUNT_1_BIT;
    pipeline_ = DemoRhi::createGraphicsPipeline(ctx, info);
}

void StochasticDemo::destroyPipelines() {
    auto &ctx = app_.ctx();
    if (pipeline_) {
        vkDestroyPipeline(ctx.device(), pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }
}

void StochasticDemo::updateDescriptors() {
    auto &ctx = app_.ctx();
    const auto &objects = scene_.objects();
    for (size_t i = 0; i < objects.size(); ++i) {
        writeMeshDescriptor(ctx, objectSets_[i], vertexUbo_, objects[i].diffuseView, scene_.sampler());
    }
}

/** 与 OpenGL processInput 一致：按住 Left/Right 持续旋转 */
void StochasticDemo::processInput() {
    GLFWwindow *window = app_.ctx().window();
    if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS) {
        camera_.orbitAngleDeg -= 1.0f;
    } else if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) {
        camera_.orbitAngleDeg += 1.0f;
    }
}

/**
 * 绘制场景全部物体（与 OpenGL renderScene 顺序一致）。
 * 每个 draw 更新 push.frameID，避免不同物体/帧的采样掩码模式重复。
 */
void StochasticDemo::drawScene(VkCommandBuffer cmd, VkExtent2D extent) {
    VkViewport viewport{0, 0, static_cast<float>(extent.width), static_cast<float>(extent.height), 0, 1};
    VkRect2D scissor{{0, 0}, extent};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);

    const auto &objects = scene_.objects();
    const int modelCnt = static_cast<int>(objects.size());
    static int frameID = 0;

    PushConstants push{};
    push.sampleCnt = sampleCount_;

    for (size_t i = 0; i < objects.size(); ++i) {
        const auto &obj = objects[i];
        push.model = obj.model;
        push.frameID = (frameID++) % modelCnt;

        vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(PushConstants), &push);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1, &objectSets_[i], 0,
                                nullptr);

        VkBuffer vb[] = {obj.mesh.vertexBuffer};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cmd, 0, 1, vb, offsets);
        vkCmdBindIndexBuffer(cmd, obj.mesh.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, obj.mesh.indexCount, 1, 0, 0, 0);
    }
}

void StochasticDemo::render(DemoFrame &frame) {
    processInput();

    const auto extent = frame.extent;
    camera_.setAspectFromViewport(extent.width, extent.height);

    VertexUbo vertexUbo{};
    vertexUbo.view = camera_.view();
    vertexUbo.proj = camera_.projection();

    void *mapped = nullptr;
    vkMapMemory(app_.ctx().device(), vertexUboMemory_, 0, sizeof(VertexUbo), 0, &mapped);
    std::memcpy(mapped, &vertexUbo, sizeof(VertexUbo));
    vkUnmapMemory(app_.ctx().device(), vertexUboMemory_);

    const glm::vec3 bg = AppConfig::backgroundColor();
    const bool useMsaa = msaaSamples_ != VK_SAMPLE_COUNT_1_BIT && !msaaFramebuffers_.empty();

    // MSAA RenderPass 需 3 个 ClearValue（attachment 0 颜色 + attachment 2 深度；索引 1 为 Resolve，DONT_CARE 但仍占位）
    VkClearValue msaaClears[3]{};
    msaaClears[0].color = {{bg.r, bg.g, bg.b, 1.0f}};
    msaaClears[1].color = {{bg.r, bg.g, bg.b, 1.0f}};
    msaaClears[2].depthStencil = {1.0f, 0};

    VkClearValue swapClears[2]{};
    swapClears[0].color = {{bg.r, bg.g, bg.b, 1.0f}};
    swapClears[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo rpBegin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rpBegin.renderPass = useMsaa ? msaaRenderPass_ : app_.swapRenderPass();
    rpBegin.framebuffer = useMsaa ? msaaFramebuffers_[frame.imageIndex] : frame.framebuffer;
    rpBegin.renderArea.extent = extent;
    rpBegin.clearValueCount = useMsaa ? 3u : 2u;
    rpBegin.pClearValues = useMsaa ? msaaClears : swapClears;

    vkCmdBeginRenderPass(frame.cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
    drawScene(frame.cmd, extent);
    vkCmdEndRenderPass(frame.cmd);
}
