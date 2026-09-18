#ifndef LINKED_LIST_DEMO_HPP
#define LINKED_LIST_DEMO_HPP

// =============================================================================
// LinkedListDemo — OpenGL LinkedListOITApp 的 Vulkan 三 Pass 实现
// =============================================================================
//
// 每帧 render() 流程（与 OpenGL 一一对应）：
//
//   Pass 1 — Opaque
//     spot → opaqueColor (RGBA16F) + opaqueDepth
//     管线: depthTest=ON, depthWrite=ON
//
//   Pass 2 — OIT Build
//     resetOitBuffers: head=0xFFFFFFFF, counter=0
//     3× quad → oit_build.frag 头插 SSBO 链表
//     管线: depthTest=OFF（深度在 Shader 内 texelFetch 采样 opaqueDepth）
//
//   Pass 3 — Composite
//     barrierOitForComposite: SSBO 写→读 + opaqueColor 可读
//     全屏 composite.frag → Swapchain
//
// OIT 缓冲：
//   headBuffer_         每像素 uint 头指针（SSBO binding 4 / composite binding 1）
//   nodeCounterBuffer_  全局节点计数（SSBO binding 5）
//   nodeDataBuffer_     节点池 Node{color,depth,next}（SSBO binding 6 / composite binding 2）
//
// Descriptor：每个 SceneObject 独立 litSet_ / oitBuildSet_（diffuse 纹理不同）。
//
// =============================================================================

#include "AppConfig.hpp"
#include "DemoApp.hpp"
#include "OitScene.hpp"
#include "OrbitCamera.hpp"

#include <vector>

class LinkedListDemo {
public:
    explicit LinkedListDemo(DemoApp &app);
    ~LinkedListDemo();

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
    void createOitBuffers();
    void destroyOitBuffers();
    void createPipelines();
    void destroyPipelines();
    void updateDescriptors();

    /// Pass2 开始前：head 清为 0xFFFFFFFF（空链），counter 清 0
    void resetOitBuffers(VkCommandBuffer cmd);
    /// Pass1 结束 → Pass2 采样 opaque color/depth
    void barrierOpaqueForSample(VkCommandBuffer cmd);
    /// Pass2 结束 → Pass3 读 SSBO 链表 + opaque color
    void barrierOitForComposite(VkCommandBuffer cmd);

    void drawLitObjects(VkCommandBuffer cmd, VkPipeline pipeline, VkPipelineLayout layout, VkExtent2D extent,
                        size_t beginIndex, size_t endIndex, bool oitBuild);
    void drawComposite(VkCommandBuffer cmd, VkExtent2D extent);

    DemoApp &app_;
    OitScene::Scene scene_;
    OrbitCamera camera_;

    VkBuffer vertexUbo_ = VK_NULL_HANDLE;
    VkDeviceMemory vertexUboMemory_ = VK_NULL_HANDLE;
    VkBuffer fragUbo_ = VK_NULL_HANDLE;
    VkDeviceMemory fragUboMemory_ = VK_NULL_HANDLE;

    // --- OIT SSBO 三件套 ---
    VkBuffer headBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory headMemory_ = VK_NULL_HANDLE;
    VkBuffer nodeCounterBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory nodeCounterMemory_ = VK_NULL_HANDLE;
    VkBuffer nodeDataBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory nodeDataMemory_ = VK_NULL_HANDLE;

    ColorAttachment opaqueColor_{};
    VkImage opaqueDepth_ = VK_NULL_HANDLE;
    VkDeviceMemory opaqueDepthMemory_ = VK_NULL_HANDLE;
    VkImageView opaqueDepthView_ = VK_NULL_HANDLE;
    ColorAttachment oitDummyColor_{}; // Pass2 FBO 占位色附件（片元 discard，不写色）

    VkFramebuffer opaqueFramebuffer_ = VK_NULL_HANDLE;
    VkFramebuffer oitFramebuffer_ = VK_NULL_HANDLE;

    VkRenderPass opaqueRenderPass_ = VK_NULL_HANDLE;
    VkRenderPass oitBuildRenderPass_ = VK_NULL_HANDLE;

    VkSampler colorSampler_ = VK_NULL_HANDLE;
    VkSampler depthSampler_ = VK_NULL_HANDLE;

    VkDescriptorSetLayout litLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout oitBuildLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout compositeLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;

    std::vector<VkDescriptorSet> litSets_;
    std::vector<VkDescriptorSet> oitBuildSets_;
    VkDescriptorSet compositeSet_ = VK_NULL_HANDLE;

    VkShaderModule litVert_ = VK_NULL_HANDLE;
    VkShaderModule litFrag_ = VK_NULL_HANDLE;
    VkShaderModule oitBuildVert_ = VK_NULL_HANDLE;
    VkShaderModule oitBuildFrag_ = VK_NULL_HANDLE;
    VkShaderModule compositeVert_ = VK_NULL_HANDLE;
    VkShaderModule compositeFrag_ = VK_NULL_HANDLE;

    VkPipelineLayout litPipelineLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout oitBuildPipelineLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout compositePipelineLayout_ = VK_NULL_HANDLE;

    VkPipeline opaquePipeline_ = VK_NULL_HANDLE;
    VkPipeline oitBuildPipeline_ = VK_NULL_HANDLE;
    VkPipeline compositePipeline_ = VK_NULL_HANDLE;

    uint32_t pixelCount_ = 0;
    uint32_t maxNodes_ = 0;
};

#endif
