#ifndef OIT_SCENE_HPP
#define OIT_SCENE_HPP

// =============================================================================
// OitScene — 深度剥离演示场景
// =============================================================================
//
// 对应 OpenGL DepthPeelingApp::initScene / drawSceneLayer：
//   - spot.obj  @ (0, 0, 0)       实体小牛（alpha≈1 时自动遮挡后续层）
//   - quad.obj  × 3               半透明彩色窗格（window-r/g/b.png）
//
// 模型加载与 Vulkan_Rendering_Paths::Scene::loadModelMesh 相同 Assimp flags；
// 勿加 aiProcess_JoinIdenticalVertices，否则 spot 网格会异常。
//
// =============================================================================

#include "AppConfig.hpp"
#include "VulkanContext.hpp"

#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace OitScene {

struct GpuTexture {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
};

struct GpuMesh {
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory indexMemory = VK_NULL_HANDLE;
    uint32_t indexCount = 0;
};

struct SceneObject {
    GpuMesh mesh;
    glm::mat4 model{1.0f};
    VkImageView diffuseView = VK_NULL_HANDLE; // Peel Pass 采样漫反射 + Alpha
};

class Scene {
public:
    bool init(VulkanContext &ctx);
    void shutdown();

    const std::vector<SceneObject> &objects() const { return objects_; }
    VkSampler sampler() const { return sampler_; }

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
