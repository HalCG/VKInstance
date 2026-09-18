// =============================================================================
// RenderingPathsApp 实现
// =============================================================================
//
// 头文件 RenderingPathsApp.hpp 含：一帧流程图、双下标同步、Descriptor 更新规则。
// 本文件实现：Swapchain 目标创建/销毁、renderFrame()、输入与窗口回调。
//
// 【成员职责速查】
//   ctx_                  VulkanContext（Instance/Device/Swapchain，来自 Vulkan_Common）
//   swapchainRenderPass_  最终呈现用的 RP（颜色 → PRESENT_SRC，带深度附件）
//   commandBuffers_[2]    双缓冲命令缓冲，与 currentFrame_ 一一对应
//   forward_/deferred_/forwardPlus_  三种渲染器，init 时全部创建，运行时 switch 调度
//   scene_/lights_        共享场景与光源，bindResources() 在 init 时各 Renderer 各绑一次
//
// =============================================================================

#include "RenderingPathsApp.hpp"

#include "VulkanBuffer.hpp"
#include "VulkanRhi.hpp"
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <iostream>
#include <sstream>

RenderingPathsApp *RenderingPathsApp::s_instance_ = nullptr;

namespace {
const char *pathName(RenderPath path) {
    switch (path) {
    case RenderPath::Forward:
        return "Forward";
    case RenderPath::Deferred:
        return "Deferred";
    case RenderPath::ForwardPlus:
        return "Forward+";
    }
    return "Unknown";
}
} // namespace

bool RenderingPathsApp::init() {
    s_instance_ = this;

    glfwSetFramebufferSizeCallback(ctx_.window(), framebufferSizeCallback);
    glfwSetWindowRefreshCallback(ctx_.window(), windowRefreshCallback);
    glfwSetKeyCallback(ctx_.window(), keyCallback);
    glfwSetMouseButtonCallback(ctx_.window(), mouseButtonCallback);
    glfwSetCursorPosCallback(ctx_.window(), cursorPosCallback);
    glfwSetScrollCallback(ctx_.window(), scrollCallback);

    std::cout << "[Engine Log] Initializing Vulkan Rendering Paths App..." << std::endl;

    if (!createSwapchainTargets()) {
        return false;
    }
    if (!scene_.init(ctx_)) {
        return false;
    }

    lights_.init(ctx_);
    lights_.regenerate(AppConfig::kLightCountPresets[lightPresetIndex_]);

    if (!forward_.init(ctx_, swapchainRenderPass_) || !forwardPlus_.init(ctx_, swapchainRenderPass_) ||
        !deferred_.init(ctx_, swapchainRenderPass_)) {
        return false;
    }
    forward_.bindResources(scene_, lights_);
    forwardPlus_.bindResources(scene_, lights_);
    deferred_.bindResources(scene_, lights_);

    refreshOverlayTitle();
    std::cout << "[Engine Log] Initialization complete. Active Path: " << pathName(currentPath_)
              << ", Lights: " << lights_.activeCount() << std::endl;
    return true;
}

// 创建与 Swapchain 绑定的渲染目标：RenderPass、深度图、每 Swapchain 图像一个 FB、同步对象
//
// 【为何深度图只有一张、却要 N 个 Framebuffer】
//   Swapchain 有 N 张颜色图（通常 3），每帧 Acquire 得到 imageIndex。
//   每个 FB 把「第 i 张 Swapchain 颜色视图」+「共享深度图」绑到同一 RenderPass。
//   深度在每帧绘制前 CLEAR，因此多 FB 共享一张深度图是常见做法。
//
// 【Semaphore / Fence 数量为何不对称】
//   imageAvailable：与 CPU 帧槽 currentFrame_ 绑定（长度 2）
//   renderFinished：与 Swapchain 图像 imageIndex 绑定（长度 N），避免不同图像 signal 同一 sem
//   inFlightFences：与 CPU 帧槽绑定；imagesInFlight_[imageIndex] 记录该图当前被哪把 fence 占用
bool RenderingPathsApp::createSwapchainTargets() {
    swapchainRenderPass_ = VulkanRhi::createSwapchainRenderPass(ctx_);
    const auto extent = ctx_.swapChainExtent();

    // 与 Swapchain 同分辨率的深度附件（D32），Forward/Deferred Pass2/Forward+ 共用
    VulkanUtil::createImage(ctx_, extent.width, extent.height, 1, VK_SAMPLE_COUNT_1_BIT,
                            VulkanUtil::findDepthFormat(ctx_), VK_IMAGE_TILING_OPTIMAL,
                            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                            depthImage_, depthMemory_);
    depthView_ = VulkanUtil::createImageView(ctx_, depthImage_, VulkanUtil::findDepthFormat(ctx_),
                                              VK_IMAGE_ASPECT_DEPTH_BIT, 1);

    // 每个 Swapchain ImageView 对应一个 Framebuffer（颜色附件槽 0 + 深度槽 1）
    swapchainFramebuffers_.resize(ctx_.swapChainImageViews().size());
    for (size_t i = 0; i < ctx_.swapChainImageViews().size(); ++i) {
        swapchainFramebuffers_[i] =
            VulkanRhi::createFramebuffer(ctx_, swapchainRenderPass_, ctx_.swapChainImageViews()[i], depthView_, extent);
    }

    // 双缓冲：最多 2 条命令缓冲同时在 GPU 飞行
    commandBuffers_.resize(kMaxFramesInFlight);
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = ctx_.commandPool();
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers_.size());
    vkAllocateCommandBuffers(ctx_.device(), &allocInfo, commandBuffers_.data());

    const size_t imageCount = ctx_.swapChainImageViews().size();
    imageAvailableSemaphores_.resize(kMaxFramesInFlight);
    renderFinishedSemaphores_.resize(imageCount);
    inFlightFences_.resize(kMaxFramesInFlight);
    imagesInFlight_.assign(imageCount, VK_NULL_HANDLE);

    VkSemaphoreCreateInfo semInfo{};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    // 初始为 SIGNALED：第一帧 wait 不会阻塞（尚未提交过任何工作）
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < imageCount; ++i) {
        vkCreateSemaphore(ctx_.device(), &semInfo, nullptr, &renderFinishedSemaphores_[i]);
    }
    for (int i = 0; i < kMaxFramesInFlight; ++i) {
        vkCreateSemaphore(ctx_.device(), &semInfo, nullptr, &imageAvailableSemaphores_[i]);
        vkCreateFence(ctx_.device(), &fenceInfo, nullptr, &inFlightFences_[i]);
    }
    return true;
}

void RenderingPathsApp::destroySwapchainTargets() {
    vkDeviceWaitIdle(ctx_.device());
    for (auto fence : inFlightFences_) {
        vkDestroyFence(ctx_.device(), fence, nullptr);
    }
    for (auto sem : imageAvailableSemaphores_) {
        vkDestroySemaphore(ctx_.device(), sem, nullptr);
    }
    for (auto sem : renderFinishedSemaphores_) {
        vkDestroySemaphore(ctx_.device(), sem, nullptr);
    }
    inFlightFences_.clear();
    imageAvailableSemaphores_.clear();
    renderFinishedSemaphores_.clear();
    imagesInFlight_.clear();

    if (!commandBuffers_.empty()) {
        vkFreeCommandBuffers(ctx_.device(), ctx_.commandPool(), static_cast<uint32_t>(commandBuffers_.size()),
                             commandBuffers_.data());
        commandBuffers_.clear();
    }
    for (auto fb : swapchainFramebuffers_) {
        vkDestroyFramebuffer(ctx_.device(), fb, nullptr);
    }
    swapchainFramebuffers_.clear();
    if (depthView_) {
        vkDestroyImageView(ctx_.device(), depthView_, nullptr);
        vkDestroyImage(ctx_.device(), depthImage_, nullptr);
        vkFreeMemory(ctx_.device(), depthMemory_, nullptr);
        depthView_ = VK_NULL_HANDLE;
    }
    if (swapchainRenderPass_) {
        vkDestroyRenderPass(ctx_.device(), swapchainRenderPass_, nullptr);
        swapchainRenderPass_ = VK_NULL_HANDLE;
    }
}

// 窗口 resize / OUT_OF_DATE 时重建 Swapchain 及所有依赖 extent 的对象
// 注意：G-Buffer（Deferred）由 deferred_.resize() 单独处理，不销毁三个 Renderer 的 Pipeline
void RenderingPathsApp::recreateSwapchain() {
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
    destroySwapchainTargets();
    ctx_.recreateSwapChain(nullptr, nullptr);
    width_ = static_cast<unsigned int>(width);
    height_ = static_cast<unsigned int>(height);
    createSwapchainTargets();
    deferred_.resize(width, height);
    framebufferResized_ = false;

    const double elapsedMs = (glfwGetTime() - startT) * 1000.0;
    std::cout << "[Swapchain Log] Swapchain recreated successfully in " << elapsedMs << " ms." << std::endl;
}

void RenderingPathsApp::refreshOverlayTitle() {
    cachedOverlayText_ = buildOverlayText();
    updateWindowTitle();
}

FrameCamera RenderingPathsApp::buildCamera() const {
    FrameCamera camera;
    camera.eye = camera_.eye();
    camera.view = camera_.viewMatrix();
    const float aspect =
        static_cast<float>(width_ > 0 ? width_ : 1) / static_cast<float>(height_ > 0 ? height_ : 1);
    camera.projection =
        glm::perspective(glm::radians(AppConfig::kFovDegrees), aspect, AppConfig::kNearPlane, AppConfig::kFarPlane);
    // GLM 默认 OpenGL 式投影；Vulkan NDC Y 向下，需翻转 Y 轴
    camera.projection[1][1] *= -1.0f;
    return camera;
}

// 单帧渲染：Acquire → Record → Submit → Present（详见头文件流程图）
void RenderingPathsApp::renderFrame() {
    if (framebufferResized_) {
        recreateSwapchain();
    }

    // 等待本 CPU 帧槽上一轮提交完成（Fence 与 currentFrame_ 绑定，而非 imageIndex）
    vkWaitForFences(ctx_.device(), 1, &inFlightFences_[currentFrame_], VK_TRUE, UINT64_MAX);

    uint32_t imageIndex = 0;
    VkResult acquireResult = vkAcquireNextImageKHR(ctx_.device(), ctx_.swapChain(), UINT64_MAX,
                                                   imageAvailableSemaphores_[currentFrame_], VK_NULL_HANDLE, &imageIndex);
    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
        std::cout << "[Swapchain Log] vkAcquireNextImageKHR returned VK_ERROR_OUT_OF_DATE_KHR" << std::endl;
        recreateSwapchain();
        return;
    }

    // 若该 Swapchain 图像仍被更早的帧占用，额外等待（双缓冲时常见）
    if (imagesInFlight_[imageIndex] != VK_NULL_HANDLE) {
        vkWaitForFences(ctx_.device(), 1, &imagesInFlight_[imageIndex], VK_TRUE, UINT64_MAX);
    }

    vkResetFences(ctx_.device(), 1, &inFlightFences_[currentFrame_]);
    vkResetCommandBuffer(commandBuffers_[currentFrame_], 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(commandBuffers_[currentFrame_], &beginInfo);

    perf_.beginFrame();
    const FrameCamera camera = buildCamera();
    const VkExtent2D extent = ctx_.swapChainExtent();

    switch (currentPath_) {
    case RenderPath::Forward:
        forward_.render(commandBuffers_[currentFrame_], scene_, lights_, camera, swapchainFramebuffers_[imageIndex],
                        extent, perf_, enableHDR_);
        break;
    case RenderPath::ForwardPlus:
        forwardPlus_.render(commandBuffers_[currentFrame_], scene_, lights_, camera, swapchainFramebuffers_[imageIndex],
                            extent, perf_, enableHDR_);
        break;
    case RenderPath::Deferred:
        deferred_.render(commandBuffers_[currentFrame_], scene_, lights_, camera, swapchainFramebuffers_[imageIndex],
                         extent, perf_, showGBufferDebug_, enableHDR_);
        break;
    }

    perf_.endFrame();
    vkEndCommandBuffer(commandBuffers_[currentFrame_]);

    // Submit：在 COLOR_ATTACHMENT_OUTPUT 阶段等待 imageAvailable，完成后 signal renderFinished
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &imageAvailableSemaphores_[currentFrame_];
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffers_[currentFrame_];
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &renderFinishedSemaphores_[imageIndex];
    vkQueueSubmit(ctx_.graphicsQueue(), 1, &submitInfo, inFlightFences_[currentFrame_]);
    imagesInFlight_[imageIndex] = inFlightFences_[currentFrame_];

    // Present：必须等待 renderFinished，确保颜色写入完成
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderFinishedSemaphores_[imageIndex];
    VkSwapchainKHR swapchain = ctx_.swapChain();
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain;
    presentInfo.pImageIndices = &imageIndex;
    VkResult presentResult = vkQueuePresentKHR(ctx_.presentQueue(), &presentInfo);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || framebufferResized_) {
        std::cout << "[Swapchain Log] vkQueuePresentKHR returned "
                  << (presentResult == VK_ERROR_OUT_OF_DATE_KHR ? "VK_ERROR_OUT_OF_DATE_KHR" : "Window Resize Event")
                  << std::endl;
        recreateSwapchain();
    }

    currentFrame_ = (currentFrame_ + 1) % kMaxFramesInFlight;

    const double now = glfwGetTime();
    if (now - lastTitleUpdate_ > 0.5) {
        lastTitleUpdate_ = now;
        refreshOverlayTitle();
    }
}

void RenderingPathsApp::run() {
    std::cout << "[Engine Log] Starting render loop..." << std::endl;
    static double lastPerfLogTime = glfwGetTime();
    static int frameCounter = 0;

    while (!glfwWindowShouldClose(ctx_.window())) {
        glfwPollEvents();
        renderFrame();

        frameCounter++;
        const double now = glfwGetTime();
        if (now - lastPerfLogTime >= 1.0) {
            const FrameStats stats = perf_.latest();
            const double fps = static_cast<double>(frameCounter) / (now - lastPerfLogTime);
            std::cout << "[Perf Log] Path: " << pathName(currentPath_)
                      << " | Lights: " << lights_.activeCount()
                      << " | FPS: " << static_cast<int>(fps + 0.5)
                      << " | Frame Time: " << stats.totalFrameMs << " ms"
                      << " | Geom: " << stats.geometryPassMs << " ms"
                      << " | Light: " << stats.lightingPassMs << " ms"
                      << " | Cull: " << stats.cullPassMs << " ms"
                      << " | Shade: " << stats.shadingPassMs << " ms"
                      << std::endl;
            frameCounter = 0;
            lastPerfLogTime = now;
        }
    }
    vkDeviceWaitIdle(ctx_.device());
}

void RenderingPathsApp::shutdown() {
    deferred_.shutdown();
    forwardPlus_.shutdown();
    forward_.shutdown();
    lights_.shutdown();
    scene_.shutdown();
    destroySwapchainTargets();
    s_instance_ = nullptr;
}

int RenderingPathsApp::nextLightPreset(int delta) const {
    int idx = lightPresetIndex_ + delta;
    idx = std::max(0, std::min(idx, AppConfig::kLightPresetCount - 1));
    return idx;
}

std::string RenderingPathsApp::buildOverlayText() const {
    const FrameStats stats = perf_.latest();
    std::ostringstream oss;
    oss << pathName(currentPath_) << " | lights=" << lights_.activeCount() << " | objects=" << scene_.objectCount()
        << " | FPS=" << static_cast<int>(stats.fps) << " | frame=" << stats.totalFrameMs << "ms";
    if (currentPath_ == RenderPath::Forward) {
        oss << " | forward=" << stats.forwardPassMs << "ms";
    } else if (currentPath_ == RenderPath::ForwardPlus) {
        oss << " | cull=" << stats.cullPassMs << "ms shade=" << stats.shadingPassMs << "ms";
    } else {
        oss << " | geom=" << stats.geometryPassMs << "ms light=" << stats.lightingPassMs << "ms";
        if (showGBufferDebug_) {
            oss << " | GBufferDebug";
        }
    }
    oss << " | HDR:" << (enableHDR_ ? "ON" : "OFF");
    oss << " | 1/2/3 path, [/] lights, G gbuffer, H hdr";
    return oss.str();
}

void RenderingPathsApp::updateWindowTitle() {
    glfwSetWindowTitle(ctx_.window(), cachedOverlayText_.c_str());
}

void RenderingPathsApp::framebufferSizeCallback(GLFWwindow * /*window*/, int width, int height) {
    if (width <= 0 || height <= 0 || !s_instance_) {
        return;
    }
    s_instance_->width_ = static_cast<unsigned int>(width);
    s_instance_->height_ = static_cast<unsigned int>(height);
    s_instance_->framebufferResized_ = true;
}

void RenderingPathsApp::windowRefreshCallback(GLFWwindow * /*window*/) {
    if (s_instance_ && !s_instance_->framebufferResized_) {
        s_instance_->renderFrame();
    }
}

void RenderingPathsApp::mouseButtonCallback(GLFWwindow *window, int button, int action, int mods) {
    (void)mods;
    if (!s_instance_) {
        return;
    }
    CameraDragMode mode = CameraDragMode::None;
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        mode = CameraDragMode::Rotate;
    } else if (button == GLFW_MOUSE_BUTTON_MIDDLE) {
        mode = CameraDragMode::Pan;
    } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        mode = CameraDragMode::Dolly;
    } else {
        return;
    }

    if (action == GLFW_PRESS) {
        double x = 0.0;
        double y = 0.0;
        glfwGetCursorPos(window, &x, &y);
        s_instance_->camera_.beginDrag(mode, x, y);
    } else if (action == GLFW_RELEASE) {
        s_instance_->camera_.endDrag();
    }
}

void RenderingPathsApp::cursorPosCallback(GLFWwindow * /*window*/, double xpos, double ypos) {
    if (s_instance_) {
        s_instance_->camera_.applyCursorDelta(xpos, ypos, s_instance_->width_, s_instance_->height_);
    }
}

void RenderingPathsApp::scrollCallback(GLFWwindow * /*window*/, double /*xoffset*/, double yoffset) {
    if (s_instance_) {
        s_instance_->camera_.applyScroll(yoffset);
    }
}

void RenderingPathsApp::keyCallback(GLFWwindow *window, int key, int scancode, int action, int mods) {
    (void)scancode;
    (void)mods;
    if (action != GLFW_PRESS || !s_instance_) {
        return;
    }

    switch (key) {
    case GLFW_KEY_ESCAPE:
        glfwSetWindowShouldClose(window, true);
        break;
    case GLFW_KEY_1:
    case GLFW_KEY_F1:
        s_instance_->currentPath_ = RenderPath::Forward;
        s_instance_->showGBufferDebug_ = false;
        std::cout << "[Input Log] Key Event: Render Path changed to Forward" << std::endl;
        break;
    case GLFW_KEY_2:
    case GLFW_KEY_F2:
        s_instance_->currentPath_ = RenderPath::Deferred;
        std::cout << "[Input Log] Key Event: Render Path changed to Deferred" << std::endl;
        break;
    case GLFW_KEY_3:
    case GLFW_KEY_F3:
        s_instance_->currentPath_ = RenderPath::ForwardPlus;
        s_instance_->showGBufferDebug_ = false;
        std::cout << "[Input Log] Key Event: Render Path changed to Forward+" << std::endl;
        break;
    case GLFW_KEY_LEFT_BRACKET: {
        const int idx = s_instance_->nextLightPreset(-1);
        if (idx != s_instance_->lightPresetIndex_) {
            s_instance_->lightPresetIndex_ = idx;
            s_instance_->lights_.regenerate(AppConfig::kLightCountPresets[idx]);
            std::cout << "[Input Log] Key Event: Light count preset changed to "
                      << AppConfig::kLightCountPresets[idx] << std::endl;
        }
        break;
    }
    case GLFW_KEY_RIGHT_BRACKET: {
        const int idx = s_instance_->nextLightPreset(1);
        if (idx != s_instance_->lightPresetIndex_) {
            s_instance_->lightPresetIndex_ = idx;
            s_instance_->lights_.regenerate(AppConfig::kLightCountPresets[idx]);
            std::cout << "[Input Log] Key Event: Light count preset changed to "
                      << AppConfig::kLightCountPresets[idx] << std::endl;
        }
        break;
    }
    case GLFW_KEY_G:
        if (s_instance_->currentPath_ == RenderPath::Deferred) {
            s_instance_->showGBufferDebug_ = !s_instance_->showGBufferDebug_;
            std::cout << "[Input Log] Key Event: GBuffer Debug "
                      << (s_instance_->showGBufferDebug_ ? "Enabled" : "Disabled") << std::endl;
        }
        break;
    case GLFW_KEY_H:
        s_instance_->enableHDR_ = !s_instance_->enableHDR_;
        std::cout << "[Input Log] Key Event: HDR "
                  << (s_instance_->enableHDR_ ? "Enabled" : "Disabled") << std::endl;
        break;
    default:
        return;
    }
    s_instance_->refreshOverlayTitle();
}
