// =============================================================================
// LinkedListDemo — OpenGL LinkedListOITApp 的 Vulkan 实现
// =============================================================================
// Pass1: 不透明 spot → opaqueColor + opaqueDepth
// Pass2: 透明 quad 链表构建（片元 Shader 采样 depth 裁切 + SSBO 头插法）
// Pass3: composite 全屏 Pass（采样 opaqueColor + 遍历 SSBO 链表）
//
// 场景/模型加载与 Vulkan_OIT_Depth_Peeling 共用 OitScene。
// =============================================================================

#include "LinkedListDemo.hpp"

#include "DemoRhi.hpp"
#include "VulkanBuffer.hpp"

#include <GLFW/glfw3.h>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace {
struct LitVertexUbo {
    glm::mat4 view;
    glm::mat4 proj;
};

struct LitFragUbo {
    glm::vec4 cameraPos;
    glm::vec4 lightPos;
    glm::vec4 k;
    uint32_t maxNodes;
    uint32_t pad[3];
};

struct OitBuildPushConstants {
    glm::mat4 model;
    uint32_t width;
    uint32_t height;
};

struct WindowInfo {
    uint32_t width;
    uint32_t height;
};

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

VkDescriptorSetLayout createLitLayout(VulkanContext &ctx) {
    VkDescriptorSetLayoutBinding bindings[3]{};
    bindings[0] = {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr};
    bindings[1] = {1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    bindings[2] = {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};

    VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    info.bindingCount = 3;
    info.pBindings = bindings;

    VkDescriptorSetLayout layout;
    if (vkCreateDescriptorSetLayout(ctx.device(), &info, nullptr, &layout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create lit descriptor layout");
    }
    return layout;
}

VkDescriptorSetLayout createOitBuildLayout(VulkanContext &ctx) {
    VkDescriptorSetLayoutBinding bindings[7]{};
    bindings[0] = {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr};
    bindings[1] = {1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    bindings[2] = {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    bindings[3] = {3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    bindings[4] = {4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    bindings[5] = {5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    bindings[6] = {6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};

    VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    info.bindingCount = 7;
    info.pBindings = bindings;

    VkDescriptorSetLayout layout;
    if (vkCreateDescriptorSetLayout(ctx.device(), &info, nullptr, &layout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create OIT build descriptor layout");
    }
    return layout;
}

VkDescriptorSetLayout createCompositeLayout(VulkanContext &ctx) {
    VkDescriptorSetLayoutBinding bindings[3]{};
    bindings[0] = {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    bindings[1] = {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    bindings[2] = {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};

    VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    info.bindingCount = 3;
    info.pBindings = bindings;

    VkDescriptorSetLayout layout;
    if (vkCreateDescriptorSetLayout(ctx.device(), &info, nullptr, &layout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create composite descriptor layout");
    }
    return layout;
}

void writeLitDescriptor(VulkanContext &ctx, VkDescriptorSet set, VkBuffer vertexUbo, VkBuffer fragUbo,
                        VkImageView diffuseView, VkSampler sampler) {
    VkDescriptorBufferInfo vertexInfo{vertexUbo, 0, VK_WHOLE_SIZE};
    VkDescriptorBufferInfo fragInfo{fragUbo, 0, VK_WHOLE_SIZE};
    VkDescriptorImageInfo diffuseInfo{sampler, diffuseView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

    VkWriteDescriptorSet writes[3]{};
    writes[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 0, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                 nullptr, &vertexInfo, nullptr};
    writes[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 1, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                 nullptr, &fragInfo, nullptr};
    writes[2] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 2, 0, 1,
                 VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &diffuseInfo, nullptr, nullptr};
    vkUpdateDescriptorSets(ctx.device(), 3, writes, 0, nullptr);
}

void writeOitBuildDescriptor(VulkanContext &ctx, VkDescriptorSet set, VkBuffer vertexUbo, VkBuffer fragUbo,
                             VkImageView diffuseView, VkImageView depthView, VkBuffer headBuffer,
                             VkBuffer nodeCounterBuffer, VkBuffer nodeDataBuffer, VkSampler colorSampler,
                             VkSampler depthSampler) {
    VkDescriptorBufferInfo vertexInfo{vertexUbo, 0, VK_WHOLE_SIZE};
    VkDescriptorBufferInfo fragInfo{fragUbo, 0, VK_WHOLE_SIZE};
    VkDescriptorImageInfo diffuseInfo{colorSampler, diffuseView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorImageInfo depthInfo{depthSampler, depthView, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
    VkDescriptorBufferInfo headInfo{headBuffer, 0, VK_WHOLE_SIZE};
    VkDescriptorBufferInfo counterInfo{nodeCounterBuffer, 0, VK_WHOLE_SIZE};
    VkDescriptorBufferInfo nodeInfo{nodeDataBuffer, 0, VK_WHOLE_SIZE};

    VkWriteDescriptorSet writes[7]{};
    writes[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 0, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                 nullptr, &vertexInfo, nullptr};
    writes[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 1, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                 nullptr, &fragInfo, nullptr};
    writes[2] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 2, 0, 1,
                 VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &diffuseInfo, nullptr, nullptr};
    writes[3] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 3, 0, 1,
                 VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &depthInfo, nullptr, nullptr};
    writes[4] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 4, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                 nullptr, &headInfo, nullptr};
    writes[5] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 5, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                 nullptr, &counterInfo, nullptr};
    writes[6] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 6, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                 nullptr, &nodeInfo, nullptr};
    vkUpdateDescriptorSets(ctx.device(), 7, writes, 0, nullptr);
}

void writeCompositeDescriptor(VulkanContext &ctx, VkDescriptorSet set, VkImageView opaqueView, VkBuffer headBuffer,
                              VkBuffer nodeDataBuffer, VkSampler sampler) {
    VkDescriptorImageInfo opaqueInfo{sampler, opaqueView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorBufferInfo headInfo{headBuffer, 0, VK_WHOLE_SIZE};
    VkDescriptorBufferInfo nodeInfo{nodeDataBuffer, 0, VK_WHOLE_SIZE};

    VkWriteDescriptorSet writes[3]{};
    writes[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 0, 0, 1,
                 VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &opaqueInfo, nullptr, nullptr};
    writes[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 1, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                 nullptr, &headInfo, nullptr};
    writes[2] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 2, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                 nullptr, &nodeInfo, nullptr};
    vkUpdateDescriptorSets(ctx.device(), 3, writes, 0, nullptr);
}

VkRenderPass createOpaqueRenderPass(VulkanContext &ctx, VkFormat colorFormat, VkFormat depthFormat) {
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

// OIT 构建 Pass：仅 dummy 颜色附件；深度裁切在 Shader 中采样 opaqueDepth
VkRenderPass createOitBuildRenderPass(VulkanContext &ctx, VkFormat colorFormat) {
    VkAttachmentDescription color{};
    color.format = colorFormat;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

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

LinkedListDemo::LinkedListDemo(DemoApp &app) : app_(app) {
    camera_.orbitAngleDeg = AppConfig::kInitialOrbitAngle;
    camera_.orbitRadius = AppConfig::kCameraOrbitRadius;
}

LinkedListDemo::~LinkedListDemo() { shutdown(); }

void LinkedListDemo::init() {
    auto &ctx = app_.ctx();
    if (!scene_.init(ctx)) {
        throw std::runtime_error("Failed to initialize OIT scene (check resources/models)");
    }

    VulkanUtil::createBuffer(ctx, sizeof(LitVertexUbo), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, vertexUbo_,
                             vertexUboMemory_);
    VulkanUtil::createBuffer(ctx, sizeof(LitFragUbo), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, fragUbo_,
                             fragUboMemory_);

    litLayout_ = createLitLayout(ctx);
    oitBuildLayout_ = createOitBuildLayout(ctx);
    compositeLayout_ = createCompositeLayout(ctx);
    pool_ = DemoRhi::createDescriptorPool(ctx, static_cast<uint32_t>(scene_.objects().size() * 2 + 2));

    VkSamplerCreateInfo colorSamplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    colorSamplerInfo.magFilter = VK_FILTER_LINEAR;
    colorSamplerInfo.minFilter = VK_FILTER_LINEAR;
    colorSamplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    colorSamplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    vkCreateSampler(ctx.device(), &colorSamplerInfo, nullptr, &colorSampler_);

    VkSamplerCreateInfo depthSamplerInfo{colorSamplerInfo};
    depthSamplerInfo.magFilter = VK_FILTER_NEAREST;
    depthSamplerInfo.minFilter = VK_FILTER_NEAREST;
    vkCreateSampler(ctx.device(), &depthSamplerInfo, nullptr, &depthSampler_);

    const size_t objectCount = scene_.objects().size();
    litSets_.resize(objectCount);
    oitBuildSets_.resize(objectCount);
    for (size_t i = 0; i < objectCount; ++i) {
        litSets_[i] = DemoRhi::allocateSet(ctx, pool_, litLayout_);
        oitBuildSets_[i] = DemoRhi::allocateSet(ctx, pool_, oitBuildLayout_);
    }
    compositeSet_ = DemoRhi::allocateSet(ctx, pool_, compositeLayout_);

    litVert_ = DemoRhi::loadSpirv(ctx, "resources/shaders/lit_vert.spv");
    litFrag_ = DemoRhi::loadSpirv(ctx, "resources/shaders/blinn_phong_frag.spv");
    oitBuildVert_ = DemoRhi::loadSpirv(ctx, "resources/shaders/oit_build_vert.spv");
    oitBuildFrag_ = DemoRhi::loadSpirv(ctx, "resources/shaders/oit_build_frag.spv");
    compositeVert_ = DemoRhi::loadSpirv(ctx, "resources/shaders/composite_vert.spv");
    compositeFrag_ = DemoRhi::loadSpirv(ctx, "resources/shaders/composite_frag.spv");

    VkPushConstantRange litPush{};
    litPush.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    litPush.size = sizeof(glm::mat4);

    VkPushConstantRange oitBuildPush{};
    oitBuildPush.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    oitBuildPush.size = sizeof(OitBuildPushConstants);

    VkPushConstantRange compositePush{};
    compositePush.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    compositePush.size = sizeof(WindowInfo);

    VkPipelineLayoutCreateInfo litLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    litLayoutInfo.setLayoutCount = 1;
    litLayoutInfo.pSetLayouts = &litLayout_;
    litLayoutInfo.pushConstantRangeCount = 1;
    litLayoutInfo.pPushConstantRanges = &litPush;
    vkCreatePipelineLayout(ctx.device(), &litLayoutInfo, nullptr, &litPipelineLayout_);

    VkPipelineLayoutCreateInfo oitBuildLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    oitBuildLayoutInfo.setLayoutCount = 1;
    oitBuildLayoutInfo.pSetLayouts = &oitBuildLayout_;
    oitBuildLayoutInfo.pushConstantRangeCount = 1;
    oitBuildLayoutInfo.pPushConstantRanges = &oitBuildPush;
    vkCreatePipelineLayout(ctx.device(), &oitBuildLayoutInfo, nullptr, &oitBuildPipelineLayout_);

    VkPipelineLayoutCreateInfo compositeLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    compositeLayoutInfo.setLayoutCount = 1;
    compositeLayoutInfo.pSetLayouts = &compositeLayout_;
    compositeLayoutInfo.pushConstantRangeCount = 1;
    compositeLayoutInfo.pPushConstantRanges = &compositePush;
    vkCreatePipelineLayout(ctx.device(), &compositeLayoutInfo, nullptr, &compositePipelineLayout_);

    createOffscreenTargets();
    createOitBuffers();
    createPipelines();
    updateDescriptors();

    const auto &objects = scene_.objects();
    std::cout << "[LinkedListOIT] Scene ready: " << objectCount << " objects"
              << " | spot indices=" << objects[0].mesh.indexCount
              << " | quad indices=" << objects[1].mesh.indexCount
              << " | extent=" << ctx.swapChainExtent().width << "x" << ctx.swapChainExtent().height
              << " | maxNodes=" << maxNodes_ << std::endl;

    glfwSetWindowTitle(ctx.window(), "LearnOpenGL | Vulkan Linked-List OIT");
}

void LinkedListDemo::shutdown() {
    auto &ctx = app_.ctx();
    vkDeviceWaitIdle(ctx.device());
    destroyPipelines();
    destroyOitBuffers();
    destroyOffscreenTargets();
    scene_.shutdown();

    auto destroyShader = [&](VkShaderModule &mod) {
        if (mod) {
            vkDestroyShaderModule(ctx.device(), mod, nullptr);
            mod = VK_NULL_HANDLE;
        }
    };
    destroyShader(compositeFrag_);
    destroyShader(compositeVert_);
    destroyShader(oitBuildFrag_);
    destroyShader(oitBuildVert_);
    destroyShader(litFrag_);
    destroyShader(litVert_);

    if (compositePipelineLayout_) {
        vkDestroyPipelineLayout(ctx.device(), compositePipelineLayout_, nullptr);
        compositePipelineLayout_ = VK_NULL_HANDLE;
    }
    if (oitBuildPipelineLayout_) {
        vkDestroyPipelineLayout(ctx.device(), oitBuildPipelineLayout_, nullptr);
        oitBuildPipelineLayout_ = VK_NULL_HANDLE;
    }
    if (litPipelineLayout_) {
        vkDestroyPipelineLayout(ctx.device(), litPipelineLayout_, nullptr);
        litPipelineLayout_ = VK_NULL_HANDLE;
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
    if (oitBuildLayout_) {
        vkDestroyDescriptorSetLayout(ctx.device(), oitBuildLayout_, nullptr);
        oitBuildLayout_ = VK_NULL_HANDLE;
    }
    if (litLayout_) {
        vkDestroyDescriptorSetLayout(ctx.device(), litLayout_, nullptr);
        litLayout_ = VK_NULL_HANDLE;
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
    litSets_.clear();
    oitBuildSets_.clear();
}

void LinkedListDemo::onResize() {
    camera_.setAspectFromViewport(app_.extent().width, app_.extent().height);
    destroyPipelines();
    destroyOitBuffers();
    destroyOffscreenTargets();
    createOffscreenTargets();
    createOitBuffers();
    createPipelines();
    updateDescriptors();
}

void LinkedListDemo::onKey(int key, int action) {
    if (action != GLFW_PRESS && action != GLFW_REPEAT) {
        return;
    }
    if (key == GLFW_KEY_LEFT) {
        camera_.orbitAngleDeg += 1.0f;
    } else if (key == GLFW_KEY_RIGHT) {
        camera_.orbitAngleDeg -= 1.0f;
    }
}

void LinkedListDemo::onMouseButton(int button, int action, double x, double y) {
    camera_.onMouseButton(button, action, x, y);
}

void LinkedListDemo::onCursorPos(double x, double y, int width, int height) {
    camera_.onCursorPos(x, y, width, height);
}

void LinkedListDemo::onScroll(double yoffset) { camera_.onScroll(yoffset); }

// 离屏 FBO：
//   opaqueFramebuffer_  → opaqueColor + opaqueDepth（Pass1 写入，Pass2/3 采样）
//   oitFramebuffer_     → oitDummyColor 占位（Pass2 片元 discard，满足 Vulkan FBO 完整性）
void LinkedListDemo::createOffscreenTargets() {
    auto &ctx = app_.ctx();
    const auto extent = ctx.swapChainExtent();
    const VkFormat colorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    const VkFormat depthFormat = VulkanUtil::findDepthFormat(ctx);

    auto createColor = [&](ColorAttachment &target) {
        VulkanUtil::createImage(ctx, extent.width, extent.height, 1, VK_SAMPLE_COUNT_1_BIT, colorFormat,
                                VK_IMAGE_TILING_OPTIMAL,
                                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, target.image, target.memory);
        target.view = VulkanUtil::createImageView(ctx, target.image, colorFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);
    };
    createColor(opaqueColor_);
    createColor(oitDummyColor_);

    VulkanUtil::createImage(ctx, extent.width, extent.height, 1, VK_SAMPLE_COUNT_1_BIT, depthFormat,
                            VK_IMAGE_TILING_OPTIMAL,
                            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, opaqueDepth_, opaqueDepthMemory_);
    opaqueDepthView_ =
        VulkanUtil::createImageView(ctx, opaqueDepth_, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT, 1);

    opaqueRenderPass_ = createOpaqueRenderPass(ctx, colorFormat, depthFormat);
    oitBuildRenderPass_ = createOitBuildRenderPass(ctx, colorFormat);

    VkImageView opaqueAttachments[] = {opaqueColor_.view, opaqueDepthView_};
    VkFramebufferCreateInfo opaqueFb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    opaqueFb.renderPass = opaqueRenderPass_;
    opaqueFb.attachmentCount = 2;
    opaqueFb.pAttachments = opaqueAttachments;
    opaqueFb.width = extent.width;
    opaqueFb.height = extent.height;
    opaqueFb.layers = 1;
    vkCreateFramebuffer(ctx.device(), &opaqueFb, nullptr, &opaqueFramebuffer_);

    VkImageView oitAttachments[] = {oitDummyColor_.view};
    VkFramebufferCreateInfo oitFb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    oitFb.renderPass = oitBuildRenderPass_;
    oitFb.attachmentCount = 1;
    oitFb.pAttachments = oitAttachments;
    oitFb.width = extent.width;
    oitFb.height = extent.height;
    oitFb.layers = 1;
    vkCreateFramebuffer(ctx.device(), &oitFb, nullptr, &oitFramebuffer_);
}

void LinkedListDemo::destroyOffscreenTargets() {
    auto &ctx = app_.ctx();
    if (oitFramebuffer_) {
        vkDestroyFramebuffer(ctx.device(), oitFramebuffer_, nullptr);
        oitFramebuffer_ = VK_NULL_HANDLE;
    }
    if (opaqueFramebuffer_) {
        vkDestroyFramebuffer(ctx.device(), opaqueFramebuffer_, nullptr);
        opaqueFramebuffer_ = VK_NULL_HANDLE;
    }
    if (oitBuildRenderPass_) {
        vkDestroyRenderPass(ctx.device(), oitBuildRenderPass_, nullptr);
        oitBuildRenderPass_ = VK_NULL_HANDLE;
    }
    if (opaqueRenderPass_) {
        vkDestroyRenderPass(ctx.device(), opaqueRenderPass_, nullptr);
        opaqueRenderPass_ = VK_NULL_HANDLE;
    }
    if (opaqueDepthView_) {
        vkDestroyImageView(ctx.device(), opaqueDepthView_, nullptr);
        opaqueDepthView_ = VK_NULL_HANDLE;
    }
    if (opaqueDepth_) {
        vkDestroyImage(ctx.device(), opaqueDepth_, nullptr);
        vkFreeMemory(ctx.device(), opaqueDepthMemory_, nullptr);
        opaqueDepth_ = VK_NULL_HANDLE;
    }
    destroyColorAttachment(ctx, oitDummyColor_.image, oitDummyColor_.memory, oitDummyColor_.view);
    destroyColorAttachment(ctx, opaqueColor_.image, opaqueColor_.memory, opaqueColor_.view);
}

// 分配 OIT 三件套（OpenGL：head image + atomic counter + linkedList SSBO）：
//   headBuffer_         width*height 个 uint，初值 0xFFFFFFFF
//   nodeCounterBuffer_  单 uint，Pass2 前清零
//   nodeDataBuffer_     maxNodes * 32B（std430 Node 对齐）
void LinkedListDemo::createOitBuffers() {
    auto &ctx = app_.ctx();
    const auto extent = ctx.swapChainExtent();
    pixelCount_ = extent.width * extent.height;
    maxNodes_ = pixelCount_ * AppConfig::kMaxFragmentsPerPixel;

    const VkDeviceSize headSize = pixelCount_ * sizeof(uint32_t);
    VulkanUtil::createBuffer(ctx, headSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, headBuffer_, headMemory_);

    VulkanUtil::createBuffer(ctx, sizeof(uint32_t),
                             VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, nodeCounterBuffer_, nodeCounterMemory_);

    const VkDeviceSize nodeDataSize = maxNodes_ * 32;
    VulkanUtil::createBuffer(ctx, nodeDataSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, nodeDataBuffer_, nodeDataMemory_);
}

void LinkedListDemo::destroyOitBuffers() {
    auto &ctx = app_.ctx();
    if (headBuffer_) {
        vkDestroyBuffer(ctx.device(), headBuffer_, nullptr);
        vkFreeMemory(ctx.device(), headMemory_, nullptr);
        headBuffer_ = VK_NULL_HANDLE;
    }
    if (nodeCounterBuffer_) {
        vkDestroyBuffer(ctx.device(), nodeCounterBuffer_, nullptr);
        vkFreeMemory(ctx.device(), nodeCounterMemory_, nullptr);
        nodeCounterBuffer_ = VK_NULL_HANDLE;
    }
    if (nodeDataBuffer_) {
        vkDestroyBuffer(ctx.device(), nodeDataBuffer_, nullptr);
        vkFreeMemory(ctx.device(), nodeDataMemory_, nullptr);
        nodeDataBuffer_ = VK_NULL_HANDLE;
    }
}

void LinkedListDemo::createPipelines() {
    auto &ctx = app_.ctx();
    const auto extent = ctx.swapChainExtent();
    auto binding = litVertexBinding();

    DemoRhi::PipelineInfo opaqueInfo{};
    opaqueInfo.vert = litVert_;
    opaqueInfo.frag = litFrag_;
    opaqueInfo.renderPass = opaqueRenderPass_;
    opaqueInfo.extent = extent;
    opaqueInfo.bindings = &binding;
    opaqueInfo.bindingCount = 1;
    opaqueInfo.attributes = litVertexAttributes;
    opaqueInfo.attributeCount = 3;
    opaqueInfo.layout = litPipelineLayout_;
    opaqueInfo.depthTest = true;
    opaqueInfo.depthWrite = true;
    opaqueInfo.cullMode = VK_CULL_MODE_NONE;
    opaquePipeline_ = DemoRhi::createGraphicsPipeline(ctx, opaqueInfo);

    DemoRhi::PipelineInfo buildInfo = opaqueInfo;
    buildInfo.vert = oitBuildVert_;
    buildInfo.frag = oitBuildFrag_;
    buildInfo.renderPass = oitBuildRenderPass_;
    buildInfo.layout = oitBuildPipelineLayout_;
    // Vulkan 限制：Pass2 须采样 opaqueDepth，不能同时作 depth attachment → Shader 内 texelFetch 裁切
    buildInfo.depthTest = false;
    buildInfo.depthWrite = false;
    oitBuildPipeline_ = DemoRhi::createGraphicsPipeline(ctx, buildInfo);

    DemoRhi::PipelineInfo compositeInfo{};
    compositeInfo.vert = compositeVert_;
    compositeInfo.frag = compositeFrag_;
    compositeInfo.renderPass = app_.swapRenderPass();
    compositeInfo.extent = extent;
    compositeInfo.layout = compositePipelineLayout_;
    compositeInfo.depthTest = false;
    compositeInfo.depthWrite = false;
    compositePipeline_ = DemoRhi::createGraphicsPipeline(ctx, compositeInfo);
}

void LinkedListDemo::destroyPipelines() {
    auto &ctx = app_.ctx();
    if (compositePipeline_) {
        vkDestroyPipeline(ctx.device(), compositePipeline_, nullptr);
        compositePipeline_ = VK_NULL_HANDLE;
    }
    if (oitBuildPipeline_) {
        vkDestroyPipeline(ctx.device(), oitBuildPipeline_, nullptr);
        oitBuildPipeline_ = VK_NULL_HANDLE;
    }
    if (opaquePipeline_) {
        vkDestroyPipeline(ctx.device(), opaquePipeline_, nullptr);
        opaquePipeline_ = VK_NULL_HANDLE;
    }
}

void LinkedListDemo::updateDescriptors() {
    auto &ctx = app_.ctx();
    const auto &objects = scene_.objects();
    for (size_t i = 0; i < objects.size(); ++i) {
        writeLitDescriptor(ctx, litSets_[i], vertexUbo_, fragUbo_, objects[i].diffuseView, colorSampler_);
        writeOitBuildDescriptor(ctx, oitBuildSets_[i], vertexUbo_, fragUbo_, objects[i].diffuseView, opaqueDepthView_,
                                headBuffer_, nodeCounterBuffer_, nodeDataBuffer_, colorSampler_, depthSampler_);
    }
    writeCompositeDescriptor(ctx, compositeSet_, opaqueColor_.view, headBuffer_, nodeDataBuffer_, colorSampler_);
}

// Pass2 前每帧执行：等价 OpenGL 的 PBO 清 head + glBufferSubData 清 atomic counter
void LinkedListDemo::resetOitBuffers(VkCommandBuffer cmd) {
    vkCmdFillBuffer(cmd, headBuffer_, 0, VK_WHOLE_SIZE, 0xFFFFFFFFu);
    vkCmdFillBuffer(cmd, nodeCounterBuffer_, 0, sizeof(uint32_t), 0u);

    VkBufferMemoryBarrier barriers[2]{};
    barriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barriers[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    barriers[0].buffer = headBuffer_;
    barriers[0].offset = 0;
    barriers[0].size = VK_WHOLE_SIZE;
    barriers[1] = barriers[0];
    barriers[1].buffer = nodeCounterBuffer_;
    barriers[1].size = sizeof(uint32_t);

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 2,
                         barriers, 0, nullptr);
}

// Pass1 → Pass2：opaque color/depth 附件写完成 → 片元 Shader 可读
void LinkedListDemo::barrierOpaqueForSample(VkCommandBuffer cmd) {
    VkImageMemoryBarrier barriers[2]{};
    barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barriers[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barriers[0].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barriers[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barriers[0].image = opaqueColor_.image;
    barriers[0].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    barriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barriers[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    barriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barriers[1].oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    barriers[1].newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    barriers[1].image = opaqueDepth_;
    barriers[1].subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};

    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 2, barriers);
}

// Pass2 → Pass3：等价 OpenGL glMemoryBarrier(SSBO | IMAGE | ATOMIC)
void LinkedListDemo::barrierOitForComposite(VkCommandBuffer cmd) {
    VkBufferMemoryBarrier bufferBarriers[2]{};
    bufferBarriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    bufferBarriers[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    bufferBarriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    bufferBarriers[0].buffer = headBuffer_;
    bufferBarriers[0].size = VK_WHOLE_SIZE;
    bufferBarriers[1] = bufferBarriers[0];
    bufferBarriers[1].buffer = nodeDataBuffer_;

    VkImageMemoryBarrier imageBarrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    imageBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    imageBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    imageBarrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageBarrier.image = opaqueColor_.image;
    imageBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 2, bufferBarriers, 1, &imageBarrier);
}

void LinkedListDemo::drawLitObjects(VkCommandBuffer cmd, VkPipeline pipeline, VkPipelineLayout layout,
                                    VkExtent2D extent, size_t beginIndex, size_t endIndex, bool oitBuild) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    const auto &objects = scene_.objects();

    for (size_t i = beginIndex; i < endIndex && i < objects.size(); ++i) {
        const auto &obj = objects[i];
        const VkDescriptorSet set = oitBuild ? oitBuildSets_[i] : litSets_[i];

        if (oitBuild) {
            OitBuildPushConstants push{};
            push.model = obj.model;
            push.width = extent.width;
            push.height = extent.height;
            vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                               sizeof(OitBuildPushConstants), &push);
        } else {
            vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &obj.model);
        }

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &set, 0, nullptr);
        VkBuffer vb[] = {obj.mesh.vertexBuffer};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cmd, 0, 1, vb, offsets);
        vkCmdBindIndexBuffer(cmd, obj.mesh.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, obj.mesh.indexCount, 1, 0, 0, 0);
    }
}

void LinkedListDemo::drawComposite(VkCommandBuffer cmd, VkExtent2D extent) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, compositePipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, compositePipelineLayout_, 0, 1, &compositeSet_, 0,
                            nullptr);
    WindowInfo windowInfo{extent.width, extent.height};
    vkCmdPushConstants(cmd, compositePipelineLayout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(WindowInfo), &windowInfo);
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

void LinkedListDemo::render(DemoFrame &frame) {
    const auto extent = frame.extent;
    camera_.setAspectFromViewport(extent.width, extent.height);

    LitVertexUbo vertexUbo{camera_.view(), camera_.projection()};
    void *mapped = nullptr;
    vkMapMemory(app_.ctx().device(), vertexUboMemory_, 0, sizeof(LitVertexUbo), 0, &mapped);
    std::memcpy(mapped, &vertexUbo, sizeof(LitVertexUbo));
    vkUnmapMemory(app_.ctx().device(), vertexUboMemory_);

    LitFragUbo fragUbo{};
    fragUbo.cameraPos = glm::vec4(camera_.eye(), 1.0f);
    fragUbo.lightPos = glm::vec4(AppConfig::lightPosition(), 1.0f);
    fragUbo.k = glm::vec4(AppConfig::materialCoeffs(), 0.0f);
    fragUbo.maxNodes = maxNodes_;
    vkMapMemory(app_.ctx().device(), fragUboMemory_, 0, sizeof(LitFragUbo), 0, &mapped);
    std::memcpy(mapped, &fragUbo, sizeof(LitFragUbo));
    vkUnmapMemory(app_.ctx().device(), fragUboMemory_);

    VkViewport viewport{0, 0, static_cast<float>(extent.width), static_cast<float>(extent.height), 0, 1};
    VkRect2D scissor{{0, 0}, extent};

    // =====================================================================
    // Pass 1: 不透明 spot → opaque FBO（Blinn-Phong，depth 写入 ON）
    // =====================================================================
    VkClearValue opaqueClears[2]{};
    opaqueClears[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    opaqueClears[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo opaqueBegin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    opaqueBegin.renderPass = opaqueRenderPass_;
    opaqueBegin.framebuffer = opaqueFramebuffer_;
    opaqueBegin.renderArea.extent = extent;
    opaqueBegin.clearValueCount = 2;
    opaqueBegin.pClearValues = opaqueClears;
    vkCmdBeginRenderPass(frame.cmd, &opaqueBegin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdSetViewport(frame.cmd, 0, 1, &viewport);
    vkCmdSetScissor(frame.cmd, 0, 1, &scissor);
    drawLitObjects(frame.cmd, opaquePipeline_, litPipelineLayout_, extent, 0, 1, false);
    vkCmdEndRenderPass(frame.cmd);
    barrierOpaqueForSample(frame.cmd);

    // =====================================================================
    // Pass 2: 透明 quad ×3 → SSBO 链表（objects[1..3]，oit_build.frag）
    // =====================================================================
    resetOitBuffers(frame.cmd);

    VkRenderPassBeginInfo oitBegin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    oitBegin.renderPass = oitBuildRenderPass_;
    oitBegin.framebuffer = oitFramebuffer_;
    oitBegin.renderArea.extent = extent;
    oitBegin.clearValueCount = 0;
    vkCmdBeginRenderPass(frame.cmd, &oitBegin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdSetViewport(frame.cmd, 0, 1, &viewport);
    vkCmdSetScissor(frame.cmd, 0, 1, &scissor);
    drawLitObjects(frame.cmd, oitBuildPipeline_, oitBuildPipelineLayout_, extent, 1, scene_.objects().size(), true);
    vkCmdEndRenderPass(frame.cmd);

    // =====================================================================
    // Pass 3: composite 全屏 → Swapchain（opaque 底色 + 链表 Over 混合）
    // =====================================================================
    barrierOitForComposite(frame.cmd);

    const glm::vec3 bg = AppConfig::backgroundColor();
    VkClearValue swapClears[2]{};
    swapClears[0].color = {{bg.r, bg.g, bg.b, 1.0f}};
    swapClears[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo compositeBegin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    compositeBegin.renderPass = app_.swapRenderPass();
    compositeBegin.framebuffer = frame.framebuffer;
    compositeBegin.renderArea.extent = extent;
    compositeBegin.clearValueCount = 2;
    compositeBegin.pClearValues = swapClears;
    vkCmdBeginRenderPass(frame.cmd, &compositeBegin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdSetViewport(frame.cmd, 0, 1, &viewport);
    vkCmdSetScissor(frame.cmd, 0, 1, &scissor);
    drawComposite(frame.cmd, extent);
    vkCmdEndRenderPass(frame.cmd);
}
