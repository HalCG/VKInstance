#ifndef STOCHASTIC_OIT_SCENE_HPP
#define STOCHASTIC_OIT_SCENE_HPP

/**
 * @file OitScene.hpp
 * @brief 场景资源 — OpenGL StochasticTransparencyApp::initScene 的 Vulkan 对应实现
 *
 * 场景组成（与 OpenGL renderScene 绘制顺序一致）：
 *   1. spot.obj  @ (0, 0, 0)           — 不透明小牛
 *   2. quad.obj  @ (0.3, -0.1, -0.8)   — 蓝色透明窗
 *   3. quad.obj  @ (0.6, 0.6, -0.6)    — 绿色透明窗
 *   4. quad.obj  @ (0, 0, 0)           — 红色透明窗
 *
 * 纹理加载失败时自动生成程序化窗格纹理（中心半透明、边缘不透明）。
 */

#include "AppConfig.hpp"
#include "VulkanContext.hpp"

#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace OitScene {

/** GPU 侧 2D 纹理（RGBA8 + ImageView） */
struct GpuTexture {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
};

/** GPU 侧网格（pos + normal + uv，Device Local 顶点/索引缓冲） */
struct GpuMesh {
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory indexMemory = VK_NULL_HANDLE;
    uint32_t indexCount = 0;
};

/** 可绘制物体：网格引用 + 模型矩阵 + diffuse 纹理视图 */
struct SceneObject {
    GpuMesh mesh;
    glm::mat4 model{1.0f};
    VkImageView diffuseView = VK_NULL_HANDLE;
};

class Scene {
public:
    bool init(VulkanContext &ctx);
    void shutdown();

    const std::vector<SceneObject> &objects() const { return objects_; }
    VkSampler sampler() const { return sampler_; }

    /** translate + uniform scale，与 OpenGL modelMatrix 一致 */
    static glm::mat4 modelMatrix(const glm::vec3 &translation, float scale = AppConfig::kModelScale);

private:
    VulkanContext *ctx_ = nullptr;
    GpuMesh quadMesh_{};
    GpuMesh spotMesh_{};
    GpuTexture texSpot_{};
    GpuTexture texWindowR_{};
    GpuTexture texWindowG_{};
    GpuTexture texWindowB_{};
    VkSampler sampler_ = VK_NULL_HANDLE;
    std::vector<SceneObject> objects_;

    GpuMesh loadModel(const std::string &relativePath);
    void destroyMesh(GpuMesh &mesh);
    void destroyTexture(GpuTexture &tex);
    GpuTexture loadTextureFile(const std::string &relativePath);
    GpuTexture createWindowTexture(const glm::vec3 &tint, float centerAlpha);
};

} // namespace OitScene

#endif
