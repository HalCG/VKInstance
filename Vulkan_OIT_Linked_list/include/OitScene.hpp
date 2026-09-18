#ifndef OIT_SCENE_HPP
#define OIT_SCENE_HPP

// =============================================================================
// OitScene — spot + 3× 半透明窗格（与 DepthPeeling 共用）
// =============================================================================
//
// 对应 OpenGL LinkedListOITApp::initScene：
//   objects[0]  spot.obj  @ (0, 0, 0)           — Pass1 不透明
//   objects[1]  quad.obj  @ (-0.5, 0, 0.8)      — Pass2 红色窗格
//   objects[2]  quad.obj  @ (0.2, -0.5, -1.0)    — Pass2 绿色窗格
//   objects[3]  quad.obj  @ (0.2, 0, -0.5)       — Pass2 蓝色窗格
//
// 模型：Assimp 加载 OBJ；贴图：stb_image，缺失时回退白纹理 / 程序化窗格色块。
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
    VkImageView diffuseView = VK_NULL_HANDLE;
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
