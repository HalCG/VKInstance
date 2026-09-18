#include "vulkan_utils.h"
#include <fstream>
#include <iostream>
#include <cstring>

// ==============================================================================
// 创建 Vulkan 缓冲区及其物理显存映射 (VkBuffer + VkDeviceMemory)
// ==============================================================================
void VulkanBuffer::Create(VulkanContext* ctx, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties) {
    Destroy();
    mCtx = ctx;
    mSize = size;

    // 1. 创建 VkBuffer 资源描述句柄
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(mCtx->mDevice, &bufferInfo, nullptr, &mBuffer) != VK_SUCCESS) {
        throw std::runtime_error("创建 VkBuffer 失败!");
    }

    // 2. 查询该 Buffer 的物理显存需求 (对齐大小与内存类型)
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(mCtx->mDevice, mBuffer, &memRequirements);

    // 3. 分配显存 VkDeviceMemory
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = mCtx->FindMemoryType(memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(mCtx->mDevice, &allocInfo, nullptr, &mMemory) != VK_SUCCESS) {
        throw std::runtime_error("分配 VkDeviceMemory 显存失败!");
    }

    // 4. 将物理显存绑定到 Buffer 句柄
    vkBindBufferMemory(mCtx->mDevice, mBuffer, mMemory, 0);

    // 5. 若显存支持 Host Visible (CPU 可读写)，则预先执行内存映射
    if (properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
        vkMapMemory(mCtx->mDevice, mMemory, 0, size, 0, &mMapped);
    }
}

// ==============================================================================
// CPU 上传数据到 Buffer (若为 Device Local 则自动开启 Staging Buffer 中转)
// ==============================================================================
void VulkanBuffer::Upload(const void* data, VkDeviceSize size) {
    if (mMapped) {
        // 直接 CPU memcpy 写入 mapped 指针
        memcpy(mMapped, data, (size_t)size);
    } else {
        // 使用 CPU 可见中转 Buffer (Staging Buffer) 通过 DMA 复制至 GPU 高速 Device Local 显存
        VulkanBuffer stagingBuffer;
        stagingBuffer.Create(mCtx, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        stagingBuffer.Upload(data, size);

        VkCommandBuffer cmd = mCtx->BeginSingleTimeCommands();
        VkBufferCopy copyRegion{};
        copyRegion.size = size;
        vkCmdCopyBuffer(cmd, stagingBuffer.mBuffer, mBuffer, 1, &copyRegion);
        mCtx->EndSingleTimeCommands(cmd);
    }
}

// ==============================================================================
// 销毁与释放 Buffer 资源
// ==============================================================================
void VulkanBuffer::Destroy() {
    if (mCtx && mCtx->mDevice != VK_NULL_HANDLE) {
        if (mMapped) {
            vkUnmapMemory(mCtx->mDevice, mMemory);
            mMapped = nullptr;
        }
        if (mBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(mCtx->mDevice, mBuffer, nullptr);
            mBuffer = VK_NULL_HANDLE;
        }
        if (mMemory != VK_NULL_HANDLE) {
            vkFreeMemory(mCtx->mDevice, mMemory, nullptr);
            mMemory = VK_NULL_HANDLE;
        }
    }
}

// ==============================================================================
// 创建 2D Storage Image (供 Compute Shader 使用 imageStore 写入)
// ==============================================================================
void VulkanImage::CreateStorageImage(VulkanContext* ctx, uint32_t width, uint32_t height, VkFormat format) {
    Destroy();
    mCtx = ctx;
    mWidth = width;
    mHeight = height;
    mFormat = format;

    // 1. 创建 VkImage
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(mCtx->mDevice, &imageInfo, nullptr, &mImage) != VK_SUCCESS) {
        throw std::runtime_error("创建 Storage Image 失败!");
    }

    // 2. 分配 Device Local 高速显存
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(mCtx->mDevice, mImage, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = mCtx->FindMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(mCtx->mDevice, &allocInfo, nullptr, &mMemory) != VK_SUCCESS) {
        throw std::runtime_error("分配 Image 显存失败!");
    }

    vkBindImageMemory(mCtx->mDevice, mImage, mMemory, 0);

    // 3. 创建 VkImageView
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = mImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(mCtx->mDevice, &viewInfo, nullptr, &mImageView) != VK_SUCCESS) {
        throw std::runtime_error("创建 ImageView 失败!");
    }

    // 4. 创建纹理采样器 Sampler
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

    if (vkCreateSampler(mCtx->mDevice, &samplerInfo, nullptr, &mSampler) != VK_SUCCESS) {
        throw std::runtime_error("创建 Sampler 失败!");
    }

    // 5. 插入 Pipeline Barrier 将 Layout 从 UNDEFINED 转换到 GENERAL (供 Compute CS 读写)
    VkCommandBuffer cmd = mCtx->BeginSingleTimeCommands();
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = mImage;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    mCtx->EndSingleTimeCommands(cmd);
}

void VulkanImage::CreateColorAttachment(VulkanContext* ctx, uint32_t width, uint32_t height, VkFormat format) {
    Destroy();
    mCtx = ctx;
    mWidth = width;
    mHeight = height;
    mFormat = format;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = { width, height, 1 };
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(mCtx->mDevice, &imageInfo, nullptr, &mImage) != VK_SUCCESS) {
        throw std::runtime_error("创建 ColorAttachment Image 失败!");
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(mCtx->mDevice, mImage, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = mCtx->FindMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(mCtx->mDevice, &allocInfo, nullptr, &mMemory) != VK_SUCCESS) {
        throw std::runtime_error("分配 ColorAttachment 显存失败!");
    }
    vkBindImageMemory(mCtx->mDevice, mImage, mMemory, 0);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = mImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(mCtx->mDevice, &viewInfo, nullptr, &mImageView) != VK_SUCCESS) {
        throw std::runtime_error("创建 ColorAttachment ImageView 失败!");
    }

    VkCommandBuffer cmd = mCtx->BeginSingleTimeCommands();
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.image = mImage;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    mCtx->EndSingleTimeCommands(cmd);
}

void VulkanImage::Destroy() {
    if (mCtx && mCtx->mDevice != VK_NULL_HANDLE) {
        if (mSampler != VK_NULL_HANDLE) {
            vkDestroySampler(mCtx->mDevice, mSampler, nullptr);
            mSampler = VK_NULL_HANDLE;
        }
        if (mImageView != VK_NULL_HANDLE) {
            vkDestroyImageView(mCtx->mDevice, mImageView, nullptr);
            mImageView = VK_NULL_HANDLE;
        }
        if (mImage != VK_NULL_HANDLE) {
            vkDestroyImage(mCtx->mDevice, mImage, nullptr);
            mImage = VK_NULL_HANDLE;
        }
        if (mMemory != VK_NULL_HANDLE) {
            vkFreeMemory(mCtx->mDevice, mMemory, nullptr);
            mMemory = VK_NULL_HANDLE;
        }
    }
}

// ==============================================================================
// 读取磁盘 SPIR-V 二进制文件 (.spv) 并创建 VkShaderModule
// ==============================================================================
VkShaderModule LoadSPIRVShaderModule(VkDevice device, const char* spvPath) {
    std::ifstream file(spvPath, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        printf("无法打开 SPIR-V 文件: %s\n", spvPath);
        throw std::runtime_error("找不到 SPIR-V 着色器二进制文件!");
    }

    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = buffer.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(buffer.data());

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("创建 VkShaderModule 失败!");
    }

    return shaderModule;
}
