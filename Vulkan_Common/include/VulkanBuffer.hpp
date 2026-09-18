#ifndef VULKAN_BUFFER_HPP
#define VULKAN_BUFFER_HPP

#include "VulkanContext.hpp"

namespace VulkanUtil {

void createBuffer(
    const VulkanContext& ctx,
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags properties,
    VkBuffer& buffer,
    VkDeviceMemory& bufferMemory
);

void copyBuffer(
    const VulkanContext& ctx,
    VkBuffer srcBuffer,
    VkBuffer dstBuffer,
    VkDeviceSize size
);

void createImage(
    const VulkanContext& ctx,
    uint32_t width,
    uint32_t height,
    uint32_t mipLevels,
    VkSampleCountFlagBits numSamples,
    VkFormat format,
    VkImageTiling tiling,
    VkImageUsageFlags usage,
    VkMemoryPropertyFlags properties,
    VkImage& image,
    VkDeviceMemory& imageMemory
);

VkImageView createImageView(
    const VulkanContext& ctx,
    VkImage image,
    VkFormat format,
    VkImageAspectFlags aspectFlags,
    uint32_t mipLevels = 1
);

VkFormat findSupportedFormat(
    const VulkanContext& ctx,
    const std::vector<VkFormat>& candidates,
    VkImageTiling tiling,
    VkFormatFeatureFlags features
);

VkFormat findDepthFormat(const VulkanContext& ctx);

void transitionImageLayout(
    const VulkanContext& ctx,
    VkImage image,
    VkFormat format,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    uint32_t mipLevels = 1
);

} // namespace VulkanUtil

#endif // VULKAN_BUFFER_HPP
