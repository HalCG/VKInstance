#ifndef VULKAN_CONTEXT_HPP
#define VULKAN_CONTEXT_HPP

#include "VulkanCommon.hpp"

class VulkanContext {
public:
    VulkanContext(int width, int height, const std::string& title);
    ~VulkanContext();

    void cleanup();

    GLFWwindow* window() const { return window_; }
    VkInstance instance() const { return instance_; }
    VkPhysicalDevice physicalDevice() const { return physicalDevice_; }
    VkDevice device() const { return device_; }
    VkSurfaceKHR surface() const { return surface_; }
    VkQueue graphicsQueue() const { return graphicsQueue_; }
    VkQueue presentQueue() const { return presentQueue_; }
    VkCommandPool commandPool() const { return commandPool_; }
    
    VkSwapchainKHR swapChain() const { return swapChain_; }
    VkFormat swapChainImageFormat() const { return swapChainImageFormat_; }
    VkExtent2D swapChainExtent() const { return swapChainExtent_; }
    const std::vector<VkImageView>& swapChainImageViews() const { return swapChainImageViews_; }
    const std::vector<VkFramebuffer>& swapChainFramebuffers() const { return swapChainFramebuffers_; }

    VulkanUtil::QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device) const;
    VulkanUtil::SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device) const;

    void recreateSwapChain(void (*createFramebuffersCallback)(void*), void* userData);
    void cleanupSwapChain();

    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;

private:
    void initWindow(int width, int height, const std::string& title);
    void createInstance();
    void setupDebugMessenger();
    void createSurface();
    void pickPhysicalDevice();
    void createLogicalDevice();
    void createSwapChain();
    void createImageViews();
    void createCommandPool();

    GLFWwindow* window_ = nullptr;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;

    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;

    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    VkQueue presentQueue_ = VK_NULL_HANDLE;

    VkSwapchainKHR swapChain_ = VK_NULL_HANDLE;
    std::vector<VkImage> swapChainImages_;
    VkFormat swapChainImageFormat_;
    VkExtent2D swapChainExtent_;
    std::vector<VkImageView> swapChainImageViews_;
    std::vector<VkFramebuffer> swapChainFramebuffers_;

    VkCommandPool commandPool_ = VK_NULL_HANDLE;

    const std::vector<const char*> validationLayers_ = {
        "VK_LAYER_KHRONOS_validation"
    };

    const std::vector<const char*> deviceExtensions_ = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };

#ifdef NDEBUG
    const bool enableValidationLayers_ = false;
#else
    const bool enableValidationLayers_ = true;
#endif
};

#endif // VULKAN_CONTEXT_HPP
