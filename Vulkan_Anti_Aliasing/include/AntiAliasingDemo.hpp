#ifndef ANTI_ALIASING_DEMO_HPP
#define ANTI_ALIASING_DEMO_HPP

// =============================================================================
// AntiAliasingDemo — 四种抗锯齿模式对比
// =============================================================================
// 模式（键盘 1/2/3/4）：
//   None — 直接渲染到 Swapchain，无 AA（锯齿最明显，作基线）
//   MSAA — 硬件多重采样：颜色/深度附件 samples=4/8，Resolve 到 Swapchain 单采样图像
//   FXAA — 先渲染到离屏颜色+深度纹理，再全屏 Pass 用 fxaa.frag 后处理到 Swapchain
//   TAA  — 离屏渲染 + Halton 抖动投影 + 历史帧混合（taa.frag 重投影）
//
// 渲染资源层次：
//   Swapchain FB     — 最终呈现
//   offscreen FB     — FXAA/TAA 的几何 Pass 目标（可采样）
//   msaa FB          — MSAA 颜色（多采样）+ Resolve 颜色（Swapchain 图像）+ 多采样深度
//   historyTex_[2]   — TAA 乒乓历史颜色
//
// 依赖 Vulkan_DemoCommon 的 DemoApp（帧循环/同步）与 DemoRhi（MSAA RenderPass 等）
// =============================================================================

#include "DemoApp.hpp"
#include "DemoTypes.hpp"

#include <glm/glm.hpp>
#include <string>
#include <vector>

class AntiAliasingDemo {
public:
    struct GpuMesh {
        VkBuffer vertexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
        VkBuffer indexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory indexMemory = VK_NULL_HANDLE;
        uint32_t indexCount = 0;
    };

    struct GpuTexture {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
    };

    explicit AntiAliasingDemo(DemoApp &app);
    ~AntiAliasingDemo();

    void init();
    void shutdown();
    void onResize();
    void onKey(int key, int action);
    void render(DemoFrame &frame);
    void updateTitle();

private:
    enum class Mode { None, MSAA, FXAA, TAA };

    void createPipelines();
    void destroyPipelines();
    void createOffscreenTargets();
    void destroyOffscreenTargets();
    void createMsaaTargets();
    void destroyMsaaTargets();
    void createTaaTargets();
    void destroyTaaTargets();
    void createSceneGeometry();
    void destroySceneGeometry();
    void drawScene(VkCommandBuffer cmd, VkRenderPass rp, VkFramebuffer fb, VkExtent2D extent, VkPipeline pipeline,
                   const glm::mat4 &view, const glm::mat4 &proj, bool msaaPass);

    DemoApp &app_;
    Mode mode_ = Mode::MSAA;

    GpuMesh spotMesh_{};
    GpuMesh floorMesh_{};
    GpuMesh pyramidMesh_{};
    GpuMesh spikesMesh_{};
    GpuMesh archesMesh_{};

    GpuTexture spotTex_{};
    GpuTexture gridTex_{};
    GpuTexture whiteTex_{};

    VkBuffer ubo_ = VK_NULL_HANDLE;
    VkDeviceMemory uboMemory_ = VK_NULL_HANDLE;
    void *uboMapped_ = nullptr;
    VkDescriptorSetLayout meshSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout fxaaSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout taaSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;

    VkDescriptorSet spotSet_ = VK_NULL_HANDLE;
    VkDescriptorSet gridSet_ = VK_NULL_HANDLE;
    VkDescriptorSet colorSet_ = VK_NULL_HANDLE;
    VkDescriptorSet fxaaSet_ = VK_NULL_HANDLE;
    VkDescriptorSet taaSet_[2]{VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkSampler sampler_ = VK_NULL_HANDLE;

    VkShaderModule meshVert_ = VK_NULL_HANDLE;
    VkShaderModule meshFrag_ = VK_NULL_HANDLE;
    VkShaderModule fxaaVert_ = VK_NULL_HANDLE;
    VkShaderModule fxaaFrag_ = VK_NULL_HANDLE;
    VkShaderModule taaVert_ = VK_NULL_HANDLE;
    VkShaderModule taaFrag_ = VK_NULL_HANDLE;

    VkPipelineLayout meshLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout fxaaLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout taaLayout_ = VK_NULL_HANDLE;
    VkPipeline meshPipeline_ = VK_NULL_HANDLE;           // None 模式：直接 Swapchain
    VkPipeline offscreenMeshPipeline_ = VK_NULL_HANDLE;  // FXAA/TAA 离屏几何
    VkPipeline meshMsaaPipeline_ = VK_NULL_HANDLE;       // MSAA 多采样几何
    VkPipeline fxaaPipeline_ = VK_NULL_HANDLE;
    VkPipeline taaPipeline_ = VK_NULL_HANDLE;

    // FXAA / TAA 共用离屏目标
    VkRenderPass offscreenRp_ = VK_NULL_HANDLE;
    VkImage offscreenColor_ = VK_NULL_HANDLE;
    VkDeviceMemory offscreenColorMem_ = VK_NULL_HANDLE;
    VkImageView offscreenColorView_ = VK_NULL_HANDLE;
    VkImage offscreenDepth_ = VK_NULL_HANDLE;
    VkDeviceMemory offscreenDepthMem_ = VK_NULL_HANDLE;
    VkImageView offscreenDepthView_ = VK_NULL_HANDLE;
    VkFramebuffer offscreenFb_ = VK_NULL_HANDLE;

    // MSAA：多采样颜色 + Resolve 到 Swapchain 图像
    VkSampleCountFlagBits msaaSamples_ = VK_SAMPLE_COUNT_4_BIT;
    VkRenderPass msaaRp_ = VK_NULL_HANDLE;
    VkImage msaaColor_ = VK_NULL_HANDLE;
    VkDeviceMemory msaaColorMem_ = VK_NULL_HANDLE;
    VkImageView msaaColorView_ = VK_NULL_HANDLE;
    VkImage msaaDepth_ = VK_NULL_HANDLE;
    VkDeviceMemory msaaDepthMem_ = VK_NULL_HANDLE;
    VkImageView msaaDepthView_ = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> msaaFbs_;

    GpuTexture historyTex_[2]{};
    uint32_t taaFrameCount_ = 0;
    glm::mat4 prevViewProj_{1.0f};
    glm::vec2 currentJitter_{0.0f};
};

#endif
