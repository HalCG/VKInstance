#ifndef DEPTH_PEELING_DEMO_HPP
#define DEPTH_PEELING_DEMO_HPP

// =============================================================================
// DepthPeelingDemo — 深度剥离 OIT（Order-Independent Transparency）
// =============================================================================
//
// 对应 OpenGL：OpenGL_OIT_Depth_Peeling / DepthPeelingApp
//
// 【每帧三阶段（与 OpenGL peelAndBlend + compositeToScreen 一致）】
//
//   Phase 0  initPeelBuffers
//     - accumColor_ 清为 (0,0,0,1)   ← Front-to-Back 混合的初始透光率 A_dst=1
//     - depthPing_[0/1] 清为 0.0     ← 第一层剥离时 shader 用 z > frontDepth 判断
//
//   Phase 1  peelAndBlend（循环最多 kMaxDepthPeelLayers 层）
//     Peel  → peelColor_ + depthPing_[output]  （采样 depthPing_[input] 作上一层深度）
//     Blend → accumColor_                      （DST_ALPHA, ONE 前向混合）
//     swap  inputDepthIndex_ / outputDepthIndex_
//
//   Phase 2  compositeToScreen
//     - 全屏绘制 accumColor_ + background * accum.a → Swapchain
//
// 【离屏附件】
//   accumColor_  RGBA16F  累积颜色（Blend Pass 写入，Composite 采样）
//   peelColor_   RGBA16F  当前剥离层颜色（Peel Pass 写入，Blend Pass 采样）
//   depthPing_[2]        乒乓深度纹理（仅 Peel 阶段使用，Composite 不读深度）
//
// 【Descriptor 约定】
//   - init / resize 时 updateDescriptors() 绑定静态 ImageView
//   - 每帧仅 memcpy UBO；每层 Peel 前更新 objectSets_ 的 depth 纹理索引
//   - model 矩阵走 Push Constant（每 draw 不同）
//
// =============================================================================

#include "AppConfig.hpp"
#include "DemoApp.hpp"
#include "OitScene.hpp"
#include "OrbitCamera.hpp"

#include <vector>

class DepthPeelingDemo {
public:
    explicit DepthPeelingDemo(DemoApp &app);
    ~DepthPeelingDemo();

    void init();
    void shutdown();
    void onResize();
    void onKey(int key, int action);
    void onMouseButton(int button, int action, double x, double y);
    void onCursorPos(double x, double y, int width, int height);
    void onScroll(double yoffset);
    void render(DemoFrame &frame);

private:
    struct ColorAttachment {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
    };

    void createOffscreenTargets();
    void destroyOffscreenTargets();
    void createPipelines();
    void destroyPipelines();
    void updateDescriptors();
    void drawScene(VkCommandBuffer cmd, VkPipeline pipeline, VkExtent2D extent);
    void clearDepthPing(VkCommandBuffer cmd, int index, float depth);
    void clearAccumColor(VkCommandBuffer cmd);
    void barrierPeelToBlend(VkCommandBuffer cmd, bool accumBlended);
    void drawFullscreen(VkCommandBuffer cmd, VkPipeline pipeline, VkPipelineLayout layout, VkDescriptorSet set,
                        VkExtent2D extent);

    DemoApp &app_;
    OitScene::Scene scene_;
    OrbitCamera camera_;

    // --- 每帧 UBO（HOST_VISIBLE，render() 内 memcpy）---
    VkBuffer vertexUbo_ = VK_NULL_HANDLE;
    VkDeviceMemory vertexUboMemory_ = VK_NULL_HANDLE;
    VkBuffer fragUbo_ = VK_NULL_HANDLE;
    VkDeviceMemory fragUboMemory_ = VK_NULL_HANDLE;
    VkBuffer compositeUbo_ = VK_NULL_HANDLE;
    VkDeviceMemory compositeUboMemory_ = VK_NULL_HANDLE;

    // --- Descriptor ---
    VkDescriptorSetLayout peelLayout_ = VK_NULL_HANDLE;       // binding 0-3: UBO×2 + diffuse + inputDepth
    VkDescriptorSetLayout blendOnlyLayout_ = VK_NULL_HANDLE;  // binding 0: peelColor_
    VkDescriptorSetLayout compositeLayout_ = VK_NULL_HANDLE;    // binding 0: accum + binding 1: bg UBO
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> objectSets_; // 每个场景物体一套 Peel 描述符
    VkDescriptorSet blendSet_ = VK_NULL_HANDLE;
    VkDescriptorSet compositeSet_ = VK_NULL_HANDLE;

    // --- 离屏 Image / Framebuffer ---
    ColorAttachment accumColor_{};
    ColorAttachment peelColor_{};
    VkImage depthPing_[2]{};
    VkDeviceMemory depthPingMem_[2]{};
    VkImageView depthPingView_[2]{};
    VkFramebuffer accumFramebuffer_ = VK_NULL_HANDLE;
    VkFramebuffer peelFramebuffer_[2]{}; // peelColor_ + depthPing_[i]

    VkRenderPass peelRenderPass_ = VK_NULL_HANDLE;       // 颜色+深度 CLEAR → 供采样
    VkRenderPass accumBlendRenderPass_ = VK_NULL_HANDLE; // 颜色 LOAD（保留已有累积）

    VkSampler colorSampler_ = VK_NULL_HANDLE; // 线性，颜色纹理
    VkSampler depthSampler_ = VK_NULL_HANDLE; // 近邻，深度纹理（避免插值）

    // --- Shader / Pipeline ---
    VkShaderModule peelVert_ = VK_NULL_HANDLE;
    VkShaderModule peelFrag_ = VK_NULL_HANDLE;
    VkShaderModule blendVert_ = VK_NULL_HANDLE;
    VkShaderModule blendFrag_ = VK_NULL_HANDLE;
    VkShaderModule compositeFrag_ = VK_NULL_HANDLE;

    VkPipelineLayout peelLayoutHandle_ = VK_NULL_HANDLE;         // + push constant model
    VkPipelineLayout blendPipelineLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout compositePipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline peelPipeline_ = VK_NULL_HANDLE;
    VkPipeline blendPipeline_ = VK_NULL_HANDLE;
    VkPipeline compositePipeline_ = VK_NULL_HANDLE;

    // 乒乓深度索引：input 供 shader 读上一层深度，output 为本层 Peel 写入目标
    int inputDepthIndex_ = 0;
    int outputDepthIndex_ = 1;
};

#endif
