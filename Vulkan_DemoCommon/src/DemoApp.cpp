// DemoApp 实现 — 可复用帧循环；详见 DemoApp.hpp

#include "DemoApp.hpp"

#include "DemoRhi.hpp"
#include "VulkanBuffer.hpp"

#include <GLFW/glfw3.h>

#include <stdexcept>

DemoApp *DemoApp::instance_ = nullptr;

DemoApp::DemoApp(const std::string &title, int width, int height) : ctx_(width, height, title) {
    instance_ = this;
    glfwSetFramebufferSizeCallback(ctx_.window(), framebufferSizeCallback);
    glfwSetWindowRefreshCallback(ctx_.window(), windowRefreshCallback);
    glfwSetKeyCallback(ctx_.window(), keyCallback);
    glfwSetMouseButtonCallback(ctx_.window(), mouseButtonCallback);
    glfwSetCursorPosCallback(ctx_.window(), cursorPosCallback);
    glfwSetScrollCallback(ctx_.window(), scrollCallback);
    createTargets();
}

DemoApp::~DemoApp() {
    vkDeviceWaitIdle(ctx_.device());
    destroyTargets();
    instance_ = nullptr;
}

void DemoApp::createTargets() {
    swapRenderPass_ = DemoRhi::createSwapchainRenderPass(ctx_);

    const auto extent = ctx_.swapChainExtent();
    VulkanUtil::createImage(ctx_, extent.width, extent.height, 1, VK_SAMPLE_COUNT_1_BIT,
                            VulkanUtil::findDepthFormat(ctx_), VK_IMAGE_TILING_OPTIMAL,
                            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, depthImage_, depthMemory_);
    depthView_ = VulkanUtil::createImageView(ctx_, depthImage_, VulkanUtil::findDepthFormat(ctx_),
                                             VK_IMAGE_ASPECT_DEPTH_BIT, 1);

    framebuffers_.resize(ctx_.swapChainImageViews().size());
    for (size_t i = 0; i < ctx_.swapChainImageViews().size(); ++i) {
        framebuffers_[i] =
            DemoRhi::createFramebuffer(ctx_, swapRenderPass_, ctx_.swapChainImageViews()[i], depthView_, extent);
    }

    commandBuffers_.resize(ctx_.swapChainImageViews().size());
    VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocInfo.commandPool = ctx_.commandPool();
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers_.size());
    vkAllocateCommandBuffers(ctx_.device(), &allocInfo, commandBuffers_.data());

    const size_t imageCount = ctx_.swapChainImageViews().size();
    imageAvailable_.resize(kMaxFrames);
    renderFinished_.resize(imageCount);
    inFlightFences_.resize(kMaxFrames);
    imagesInFlight_.assign(imageCount, VK_NULL_HANDLE);

    VkSemaphoreCreateInfo semInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (size_t i = 0; i < imageCount; ++i) {
        vkCreateSemaphore(ctx_.device(), &semInfo, nullptr, &renderFinished_[i]);
    }
    for (int i = 0; i < kMaxFrames; ++i) {
        vkCreateSemaphore(ctx_.device(), &semInfo, nullptr, &imageAvailable_[i]);
        vkCreateFence(ctx_.device(), &fenceInfo, nullptr, &inFlightFences_[i]);
    }
}

void DemoApp::destroyTargets() {
    vkDeviceWaitIdle(ctx_.device());
    for (auto fence : inFlightFences_) {
        vkDestroyFence(ctx_.device(), fence, nullptr);
    }
    for (auto sem : imageAvailable_) {
        vkDestroySemaphore(ctx_.device(), sem, nullptr);
    }
    for (auto sem : renderFinished_) {
        vkDestroySemaphore(ctx_.device(), sem, nullptr);
    }
    inFlightFences_.clear();
    imageAvailable_.clear();
    renderFinished_.clear();
    imagesInFlight_.clear();
    imagesInFlight_.clear();

    if (!commandBuffers_.empty()) {
        vkFreeCommandBuffers(ctx_.device(), ctx_.commandPool(), static_cast<uint32_t>(commandBuffers_.size()),
                             commandBuffers_.data());
        commandBuffers_.clear();
    }
    for (auto fb : framebuffers_) {
        vkDestroyFramebuffer(ctx_.device(), fb, nullptr);
    }
    framebuffers_.clear();
    if (depthView_) {
        vkDestroyImageView(ctx_.device(), depthView_, nullptr);
        vkDestroyImage(ctx_.device(), depthImage_, nullptr);
        vkFreeMemory(ctx_.device(), depthMemory_, nullptr);
        depthView_ = VK_NULL_HANDLE;
    }
    if (swapRenderPass_) {
        vkDestroyRenderPass(ctx_.device(), swapRenderPass_, nullptr);
        swapRenderPass_ = VK_NULL_HANDLE;
    }
}

void DemoApp::recreateSwapchain() {
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(ctx_.window(), &width, &height);
    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(ctx_.window(), &width, &height);
        glfwWaitEvents();
    }

    std::cout << "[Swapchain Log] Recreating swapchain (" << width << "x" << height << ")..." << std::endl;
    const double startT = glfwGetTime();

    vkDeviceWaitIdle(ctx_.device());
    destroyTargets();
    ctx_.recreateSwapChain(nullptr, nullptr);
    createTargets();
    if (resizeCb_) {
        resizeCb_();
    }
    resized_ = false;

    const double elapsedMs = (glfwGetTime() - startT) * 1000.0;
    std::cout << "[Swapchain Log] Swapchain recreated in " << elapsedMs << " ms." << std::endl;
}

void DemoApp::run() {
    float time = 0.0f;
    double lastTime = glfwGetTime();

    static int frameCounter = 0;
    static double fpsLastTime = glfwGetTime();

    std::cout << "[Engine Log] Starting render loop..." << std::endl;

    while (!glfwWindowShouldClose(ctx_.window())) {
        glfwPollEvents();

        if (resized_) {
            recreateSwapchain();
            continue;
        }

        const double now = glfwGetTime();
        const float dt = static_cast<float>(now - lastTime);
        lastTime = now;
        time += dt;
        camera_.tick(dt);

        // 与 RenderingPathsApp 相同的 Acquire → Record → Submit → Present 流程
        vkWaitForFences(ctx_.device(), 1, &inFlightFences_[frameIndex_], VK_TRUE, UINT64_MAX);

        uint32_t imageIndex = 0;        VkResult acquireResult =
            vkAcquireNextImageKHR(ctx_.device(), ctx_.swapChain(), UINT64_MAX, imageAvailable_[frameIndex_],
                                  VK_NULL_HANDLE, &imageIndex);
        if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
            std::cout << "[Swapchain Log] Acquire next image returned VK_ERROR_OUT_OF_DATE_KHR" << std::endl;
            recreateSwapchain();
            continue;
        }

        if (imagesInFlight_[imageIndex] != VK_NULL_HANDLE) {
            vkWaitForFences(ctx_.device(), 1, &imagesInFlight_[imageIndex], VK_TRUE, UINT64_MAX);
        }

        vkResetFences(ctx_.device(), 1, &inFlightFences_[frameIndex_]);
        vkResetCommandBuffer(commandBuffers_[imageIndex], 0);

        VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        vkBeginCommandBuffer(commandBuffers_[imageIndex], &beginInfo);

        const auto extent = ctx_.swapChainExtent();
        DemoFrame frame;
        frame.cmd = commandBuffers_[imageIndex];
        frame.framebuffer = framebuffers_[imageIndex];
        frame.imageIndex = imageIndex;
        frame.extent = extent;
        frame.view = camera_.view();
        frame.proj = SimpleCamera::proj(static_cast<float>(extent.width) / static_cast<float>(extent.height));
        frame.time = time;

        if (frameCb_) {
            frameCb_(frame);
        }

        vkEndCommandBuffer(commandBuffers_[imageIndex]);

        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &imageAvailable_[frameIndex_];
        submitInfo.pWaitDstStageMask = &waitStage;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffers_[imageIndex];
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &renderFinished_[imageIndex];
        vkQueueSubmit(ctx_.graphicsQueue(), 1, &submitInfo, inFlightFences_[frameIndex_]);
        imagesInFlight_[imageIndex] = inFlightFences_[frameIndex_];

        VkPresentInfoKHR presentInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &renderFinished_[imageIndex];
        VkSwapchainKHR swapchain = ctx_.swapChain();
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &swapchain;
        presentInfo.pImageIndices = &imageIndex;
        VkResult presentResult = vkQueuePresentKHR(ctx_.presentQueue(), &presentInfo);
        if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || resized_) {
            std::cout << "[Swapchain Log] Present returned "
                      << (presentResult == VK_ERROR_OUT_OF_DATE_KHR ? "VK_ERROR_OUT_OF_DATE_KHR" : "Resize Event")
                      << std::endl;
            recreateSwapchain();
        }

        frameIndex_ = (frameIndex_ + 1) % kMaxFrames;

        frameCounter++;
        const double fpsNow = glfwGetTime();
        if (fpsNow - fpsLastTime >= 1.0) {
            const double fps = static_cast<double>(frameCounter) / (fpsNow - fpsLastTime);
            const double frameMs = (fpsNow - fpsLastTime) * 1000.0 / static_cast<double>(frameCounter);
            std::cout << "[Perf Log] FPS: " << static_cast<int>(fps + 0.5)
                      << " | Frame Time: " << frameMs << " ms | Extent: "
                      << extent.width << "x" << extent.height << std::endl;
            frameCounter = 0;
            fpsLastTime = fpsNow;
        }
    }
    vkDeviceWaitIdle(ctx_.device());
}

void DemoApp::framebufferSizeCallback(GLFWwindow * /*window*/, int /*width*/, int /*height*/) {
    if (instance_) {
        instance_->resized_ = true;
    }
}

void DemoApp::windowRefreshCallback(GLFWwindow * /*window*/) {
    if (instance_ && !instance_->resized_) {
        // Redraw during window move/drag modal event loop
    }
}

void DemoApp::keyCallback(GLFWwindow * /*window*/, int key, int /*scancode*/, int action, int /*mods*/) {
    if (instance_ && instance_->keyCb_) {
        instance_->keyCb_(key, action);
    }
}

void DemoApp::mouseButtonCallback(GLFWwindow * /*window*/, int button, int action, int /*mods*/) {
    if (!instance_) {
        return;
    }
    double x = 0.0, y = 0.0;
    glfwGetCursorPos(instance_->ctx_.window(), &x, &y);
    if (instance_->mouseButtonCb_) {
        instance_->mouseButtonCb_(button, action, x, y);
    } else if (instance_->builtinCameraEnabled_) {
        instance_->camera_.onMouseButton(button, action, x, y);
    }
}

void DemoApp::cursorPosCallback(GLFWwindow * /*window*/, double xpos, double ypos) {
    if (!instance_) {
        return;
    }
    const auto extent = instance_->ctx_.swapChainExtent();
    const int width = static_cast<int>(extent.width);
    const int height = static_cast<int>(extent.height);
    if (instance_->cursorCb_) {
        instance_->cursorCb_(xpos, ypos, width, height);
    } else if (instance_->builtinCameraEnabled_) {
        instance_->camera_.onCursorPos(xpos, ypos, width, height);
    }
}

void DemoApp::scrollCallback(GLFWwindow * /*window*/, double /*xoffset*/, double yoffset) {
    if (!instance_) {
        return;
    }
    if (instance_->scrollCb_) {
        instance_->scrollCb_(yoffset);
    } else if (instance_->builtinCameraEnabled_) {
        instance_->camera_.onScroll(yoffset);
    }
}
