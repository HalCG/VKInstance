#include "vulkan_pipeline.h"
#include <stdexcept>

// ==============================================================================
// 创建 Compute Compute Shader 计算管线
// ==============================================================================
void VulkanComputePipeline::Create(VulkanContext* ctx, const char* spvPath, const std::vector<VkDescriptorSetLayoutBinding>& bindings) {
    Destroy();
    mCtx = ctx;

    // 1. 从 SPIR-V 文件加载着色器模块
    mShaderModule = LoadSPIRVShaderModule(mCtx->mDevice, spvPath);

    // 2. 创建 DescriptorSetLayout (指定每个 Binding 槽位的资源类型, 如 SSBO / UBO)
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(mCtx->mDevice, &layoutInfo, nullptr, &mDescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("创建 Compute DescriptorSetLayout 失败!");
    }

    // 3. 创建 DescriptorPool
    std::vector<VkDescriptorPoolSize> poolSizes;
    for (const auto& binding : bindings) {
        VkDescriptorPoolSize poolSize{};
        poolSize.type = binding.descriptorType;
        poolSize.descriptorCount = 1;
        poolSizes.push_back(poolSize);
    }

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 1;

    if (vkCreateDescriptorPool(mCtx->mDevice, &poolInfo, nullptr, &mDescriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("创建 Compute DescriptorPool 失败!");
    }

    // 4. 分配 DescriptorSet 实例
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = mDescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &mDescriptorSetLayout;

    if (vkAllocateDescriptorSets(mCtx->mDevice, &allocInfo, &mDescriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("分配 Compute DescriptorSet 失败!");
    }

    // 5. 创建 PipelineLayout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &mDescriptorSetLayout;

    if (vkCreatePipelineLayout(mCtx->mDevice, &pipelineLayoutInfo, nullptr, &mPipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("创建 Compute PipelineLayout 失败!");
    }

    // 6. 创建 VkPipeline 物理管线对象
    VkComputePipelineCreateInfo computePipelineInfo{};
    computePipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    computePipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    computePipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    computePipelineInfo.stage.module = mShaderModule;
    computePipelineInfo.stage.pName = "main";
    computePipelineInfo.layout = mPipelineLayout;

    if (vkCreateComputePipelines(mCtx->mDevice, VK_NULL_HANDLE, 1, &computePipelineInfo, nullptr, &mPipeline) != VK_SUCCESS) {
        throw std::runtime_error("创建 Compute VkPipeline 失败!");
    }
}

// ==============================================================================
// 绑定 SSBO 缓冲区资源到指定 Binding 槽位
// ==============================================================================
void VulkanComputePipeline::BindSSBO(uint32_t binding, VkBuffer buffer, VkDeviceSize size) {
    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = buffer;
    bufferInfo.offset = 0;
    bufferInfo.range = size;

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = mDescriptorSet;
    descriptorWrite.dstBinding = binding;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pBufferInfo = &bufferInfo;

    vkUpdateDescriptorSets(mCtx->mDevice, 1, &descriptorWrite, 0, nullptr);
}

// ==============================================================================
// 绑定 UBO 常量缓冲区资源到指定 Binding 槽位
// ==============================================================================
void VulkanComputePipeline::BindUniformBuffer(uint32_t binding, VkBuffer buffer, VkDeviceSize size) {
    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = buffer;
    bufferInfo.offset = 0;
    bufferInfo.range = size;

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = mDescriptorSet;
    descriptorWrite.dstBinding = binding;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pBufferInfo = &bufferInfo;

    vkUpdateDescriptorSets(mCtx->mDevice, 1, &descriptorWrite, 0, nullptr);
}

// ==============================================================================
// 绑定 Storage Image 到指定 Binding 槽位
// ==============================================================================
void VulkanComputePipeline::BindStorageImage(uint32_t binding, VkImageView imageView) {
    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    imageInfo.imageView = imageView;
    imageInfo.sampler = VK_NULL_HANDLE;

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = mDescriptorSet;
    descriptorWrite.dstBinding = binding;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(mCtx->mDevice, 1, &descriptorWrite, 0, nullptr);
}

// ==============================================================================
// 派发 Compute Shader 工作组
// ==============================================================================
void VulkanComputePipeline::Dispatch(VkCommandBuffer cmd, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, mPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, mPipelineLayout, 0, 1, &mDescriptorSet, 0, nullptr);
    vkCmdDispatch(cmd, groupCountX, groupCountY, groupCountZ);
}

void VulkanComputePipeline::Destroy() {
    if (mCtx && mCtx->mDevice != VK_NULL_HANDLE) {
        if (mPipeline != VK_NULL_HANDLE) vkDestroyPipeline(mCtx->mDevice, mPipeline, nullptr);
        if (mPipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(mCtx->mDevice, mPipelineLayout, nullptr);
        if (mDescriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(mCtx->mDevice, mDescriptorPool, nullptr);
        if (mDescriptorSetLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(mCtx->mDevice, mDescriptorSetLayout, nullptr);
        if (mShaderModule != VK_NULL_HANDLE) vkDestroyShaderModule(mCtx->mDevice, mShaderModule, nullptr);
    }
}

// ==============================================================================
// 创建 Nanite 硬件光栅化图形管线 (无颜色缓冲区写掩码, 由 Fragment Shader 使用 atomicMin 写入 VisBuffer64)
// ==============================================================================
void VulkanGraphicsPipeline::CreateHWRasterize(VulkanContext* ctx, const char* vertSpv, const char* fragSpv, const std::vector<VkDescriptorSetLayoutBinding>& bindings) {
    Destroy();
    mCtx = ctx;

    mVertModule = LoadSPIRVShaderModule(mCtx->mDevice, vertSpv);
    mFragModule = LoadSPIRVShaderModule(mCtx->mDevice, fragSpv);

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    vkCreateDescriptorSetLayout(mCtx->mDevice, &layoutInfo, nullptr, &mDescriptorSetLayout);

    std::vector<VkDescriptorPoolSize> poolSizes;
    for (const auto& binding : bindings) {
        VkDescriptorPoolSize poolSize{};
        poolSize.type = binding.descriptorType;
        poolSize.descriptorCount = 1;
        poolSizes.push_back(poolSize);
    }

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 1;
    vkCreateDescriptorPool(mCtx->mDevice, &poolInfo, nullptr, &mDescriptorPool);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = mDescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &mDescriptorSetLayout;
    vkAllocateDescriptorSets(mCtx->mDevice, &allocInfo, &mDescriptorSet);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &mDescriptorSetLayout;
    vkCreatePipelineLayout(mCtx->mDevice, &pipelineLayoutInfo, nullptr, &mPipelineLayout);

    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = mCtx->mSwapchainImageFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    vkCreateRenderPass(mCtx->mDevice, &renderPassInfo, nullptr, &mRenderPass);

    // 离屏颜色附件 + 与 mRenderPass 匹配的 Framebuffer（禁止复用 FSQ 的 Swapchain FB）
    mOffscreenColor.CreateColorAttachment(mCtx, mCtx->mWidth, mCtx->mHeight, mCtx->mSwapchainImageFormat);
    VkImageView colorAttachmentView = mOffscreenColor.mImageView;
    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = mRenderPass;
    framebufferInfo.attachmentCount = 1;
    framebufferInfo.pAttachments = &colorAttachmentView;
    framebufferInfo.width = mCtx->mSwapchainExtent.width;
    framebufferInfo.height = mCtx->mSwapchainExtent.height;
    framebufferInfo.layers = 1;
    if (vkCreateFramebuffer(mCtx->mDevice, &framebufferInfo, nullptr, &mFramebuffer) != VK_SUCCESS) {
        throw std::runtime_error("创建 HWRasterize Framebuffer 失败!");
    }

    VkPipelineShaderStageCreateInfo vertStageInfo{};
    vertStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStageInfo.module = mVertModule;
    vertStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragStageInfo{};
    fragStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStageInfo.module = mFragModule;
    fragStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = { vertStageInfo, fragStageInfo };

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport{ 0.0f, 0.0f, (float)mCtx->mWidth, (float)mCtx->mHeight, 0.0f, 1.0f };
    VkRect2D scissor{ {0, 0}, mCtx->mSwapchainExtent };

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = 0; // 关闭帧缓冲颜色通道写入 (由 VisBuffer64 atomicMin 原生替代)

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.layout = mPipelineLayout;
    pipelineInfo.renderPass = mRenderPass;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(mCtx->mDevice, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &mPipeline) != VK_SUCCESS) {
        throw std::runtime_error("创建 HWRasterize Graphics Pipeline 失败!");
    }
}

// ==============================================================================
// 创建全屏 FSQ 呈现图形管线 (采样 VisualizationTexture 写入 Swapchain)
// ==============================================================================
void VulkanGraphicsPipeline::CreateFSQ(VulkanContext* ctx, const char* vertSpv, const char* fragSpv, const std::vector<VkDescriptorSetLayoutBinding>& bindings) {
    Destroy();
    mCtx = ctx;

    mVertModule = LoadSPIRVShaderModule(mCtx->mDevice, vertSpv);
    mFragModule = LoadSPIRVShaderModule(mCtx->mDevice, fragSpv);

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    vkCreateDescriptorSetLayout(mCtx->mDevice, &layoutInfo, nullptr, &mDescriptorSetLayout);

    std::vector<VkDescriptorPoolSize> poolSizes;
    for (const auto& binding : bindings) {
        VkDescriptorPoolSize poolSize{};
        poolSize.type = binding.descriptorType;
        poolSize.descriptorCount = 1;
        poolSizes.push_back(poolSize);
    }

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 1;
    vkCreateDescriptorPool(mCtx->mDevice, &poolInfo, nullptr, &mDescriptorPool);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = mDescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &mDescriptorSetLayout;
    vkAllocateDescriptorSets(mCtx->mDevice, &allocInfo, &mDescriptorSet);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &mDescriptorSetLayout;
    vkCreatePipelineLayout(mCtx->mDevice, &pipelineLayoutInfo, nullptr, &mPipelineLayout);

    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = mCtx->mSwapchainImageFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    vkCreateRenderPass(mCtx->mDevice, &renderPassInfo, nullptr, &mRenderPass);

    mCtx->mSwapchainFramebuffers.resize(mCtx->mSwapchainImageViews.size());
    for (size_t i = 0; i < mCtx->mSwapchainImageViews.size(); i++) {
        VkImageView attachments[] = { mCtx->mSwapchainImageViews[i] };
        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = mRenderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = mCtx->mSwapchainExtent.width;
        framebufferInfo.height = mCtx->mSwapchainExtent.height;
        framebufferInfo.layers = 1;
        vkCreateFramebuffer(mCtx->mDevice, &framebufferInfo, nullptr, &mCtx->mSwapchainFramebuffers[i]);
    }

    VkPipelineShaderStageCreateInfo vertStageInfo{};
    vertStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStageInfo.module = mVertModule;
    vertStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragStageInfo{};
    fragStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStageInfo.module = mFragModule;
    fragStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = { vertStageInfo, fragStageInfo };

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport{ 0.0f, 0.0f, (float)mCtx->mWidth, (float)mCtx->mHeight, 0.0f, 1.0f };
    VkRect2D scissor{ {0, 0}, mCtx->mSwapchainExtent };

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.layout = mPipelineLayout;
    pipelineInfo.renderPass = mRenderPass;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(mCtx->mDevice, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &mPipeline) != VK_SUCCESS) {
        throw std::runtime_error("创建 FSQ Graphics Pipeline 失败!");
    }
}

void VulkanGraphicsPipeline::BeginRenderPass(VkCommandBuffer cmd, VkFramebuffer framebuffer, VkExtent2D extent, bool clearColor, const float clearColorRGBA[4]) {
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = mRenderPass;
    renderPassInfo.framebuffer = framebuffer;
    renderPassInfo.renderArea.offset = { 0, 0 };
    renderPassInfo.renderArea.extent = extent;
    VkClearValue clearValue{};
    if (clearColor && clearColorRGBA) {
        clearValue.color = { { clearColorRGBA[0], clearColorRGBA[1], clearColorRGBA[2], clearColorRGBA[3] } };
        renderPassInfo.clearValueCount = 1;
        renderPassInfo.pClearValues = &clearValue;
    }
    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipelineLayout, 0, 1, &mDescriptorSet, 0, nullptr);
}

void VulkanGraphicsPipeline::EndRenderPass(VkCommandBuffer cmd) {
    vkCmdEndRenderPass(cmd);
}

void VulkanGraphicsPipeline::Draw(VkCommandBuffer cmd, uint32_t vertexCount, uint32_t instanceCount) {
    vkCmdDraw(cmd, vertexCount, instanceCount, 0, 0);
}

void VulkanGraphicsPipeline::DrawIndirect(VkCommandBuffer cmd, VkBuffer indirectBuffer, VkDeviceSize offset) {
    vkCmdDrawIndirect(cmd, indirectBuffer, offset, 1, sizeof(VkDrawIndirectCommand));
}

void VulkanGraphicsPipeline::BindSSBO(uint32_t binding, VkBuffer buffer, VkDeviceSize size) {
    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = buffer;
    bufferInfo.offset = 0;
    bufferInfo.range = size;

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = mDescriptorSet;
    descriptorWrite.dstBinding = binding;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pBufferInfo = &bufferInfo;

    vkUpdateDescriptorSets(mCtx->mDevice, 1, &descriptorWrite, 0, nullptr);
}

void VulkanGraphicsPipeline::BindUniformBuffer(uint32_t binding, VkBuffer buffer, VkDeviceSize size) {
    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = buffer;
    bufferInfo.offset = 0;
    bufferInfo.range = size;

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = mDescriptorSet;
    descriptorWrite.dstBinding = binding;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pBufferInfo = &bufferInfo;

    vkUpdateDescriptorSets(mCtx->mDevice, 1, &descriptorWrite, 0, nullptr);
}

void VulkanGraphicsPipeline::BindCombinedImageSampler(uint32_t binding, VkImageView imageView, VkSampler sampler) {
    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    imageInfo.imageView = imageView;
    imageInfo.sampler = sampler;

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = mDescriptorSet;
    descriptorWrite.dstBinding = binding;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(mCtx->mDevice, 1, &descriptorWrite, 0, nullptr);
}

void VulkanGraphicsPipeline::Destroy() {
    if (mCtx && mCtx->mDevice != VK_NULL_HANDLE) {
        if (mFramebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(mCtx->mDevice, mFramebuffer, nullptr);
            mFramebuffer = VK_NULL_HANDLE;
        }
        mOffscreenColor.Destroy();
        if (mPipeline != VK_NULL_HANDLE) vkDestroyPipeline(mCtx->mDevice, mPipeline, nullptr);
        if (mRenderPass != VK_NULL_HANDLE) vkDestroyRenderPass(mCtx->mDevice, mRenderPass, nullptr);
        if (mPipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(mCtx->mDevice, mPipelineLayout, nullptr);
        if (mDescriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(mCtx->mDevice, mDescriptorPool, nullptr);
        if (mDescriptorSetLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(mCtx->mDevice, mDescriptorSetLayout, nullptr);
        if (mVertModule != VK_NULL_HANDLE) vkDestroyShaderModule(mCtx->mDevice, mVertModule, nullptr);
        if (mFragModule != VK_NULL_HANDLE) vkDestroyShaderModule(mCtx->mDevice, mFragModule, nullptr);
    }
}
