#ifndef STOCHASTIC_DEMO_HPP
#define STOCHASTIC_DEMO_HPP

/**
 * @file StochasticDemo.hpp
 * @brief Vulkan 随机透明度 (Stochastic Transparency) 演示
 *
 * 渲染流程（单 Pass，与 OpenGL 版一致）：
 *   1. MSAA RenderPass：多采样颜色 + Resolve 到 Swapchain + 多采样深度
 *   2. 逐对象绘制 spot + 3 扇透明窗，Fragment Shader 写入 gl_SampleMask
 *   3. 硬件 MSAA Resolve 自动对子采样求平均，无需排序
 *
 * 资源绑定：
 *   - Descriptor Set 0：Vertex UBO (view/proj) + diffuse 纹理
 *   - Push Constants：model + frameID + sampleCnt（每 draw 更新）
 */

#include "AppConfig.hpp"
#include "DemoApp.hpp"
#include "OitScene.hpp"
#include "OrbitCamera.hpp"

#include <vector>

class StochasticDemo {
public:
    explicit StochasticDemo(DemoApp &app);
    ~StochasticDemo();

    void init();
    void shutdown();
    void onResize();
    void onMouseButton(int button, int action, double x, double y);
    void onCursorPos(double x, double y, int width, int height);
    void onScroll(double yoffset);
    void render(DemoFrame &frame);

private:
    /** 顶点阶段 UBO：每帧更新 view / proj */
    struct VertexUbo {
        glm::mat4 view;
        glm::mat4 proj;
    };

    /**
     * Push Constants：每 draw 更新
     * - model：物体世界矩阵
     * - frameID：随机数种子偏移（避免各物体采样掩码重复）
     * - sampleCnt：MSAA 采样点数（与 gl_SampleMask 循环次数一致）
     */
    struct PushConstants {
        alignas(16) glm::mat4 model;
        int32_t frameID = 0;
        int32_t sampleCnt = 1;
    };

    void createMsaaTargets();
    void destroyMsaaTargets();
    void createPipelines();
    void destroyPipelines();
    void updateDescriptors();
    void processInput();
    void drawScene(VkCommandBuffer cmd, VkExtent2D extent);

    DemoApp &app_;
    OitScene::Scene scene_;
    OrbitCamera camera_;

    /** GPU 支持的 MSAA 档位（优先 16x，与 OpenGL GLFW_SAMPLES=16 对应） */
    VkSampleCountFlagBits msaaSamples_ = VK_SAMPLE_COUNT_1_BIT;
    /** 整数采样数，传入 Fragment Shader 的 sampleCnt */
    int32_t sampleCount_ = 1;

    VkBuffer vertexUbo_ = VK_NULL_HANDLE;
    VkDeviceMemory vertexUboMemory_ = VK_NULL_HANDLE;

    VkDescriptorSetLayout meshLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    /** 每个 SceneObject 一个 descriptor set（纹理不同） */
    std::vector<VkDescriptorSet> objectSets_;

    /** MSAA 离屏目标：RenderPass + 多采样颜色/深度 + 每 Swapchain 图像一个 FB */
    VkRenderPass msaaRenderPass_ = VK_NULL_HANDLE;
    VkImage msaaColor_ = VK_NULL_HANDLE;
    VkDeviceMemory msaaColorMem_ = VK_NULL_HANDLE;
    VkImageView msaaColorView_ = VK_NULL_HANDLE;
    VkImage msaaDepth_ = VK_NULL_HANDLE;
    VkDeviceMemory msaaDepthMem_ = VK_NULL_HANDLE;
    VkImageView msaaDepthView_ = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> msaaFramebuffers_;

    VkShaderModule vert_ = VK_NULL_HANDLE;
    VkShaderModule frag_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
};

#endif
