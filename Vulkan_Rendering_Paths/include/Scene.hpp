#ifndef SCENE_HPP
#define SCENE_HPP

// =============================================================================
// Scene — 演示场景：地面 + 12 个 Spot 模型实例
// =============================================================================
//
// 【绘制统计】 objectCount() = 1（地面）+ objects_.size()（默认 12）= 13 个 Draw Call
//
// 【与 Renderer 的协作】
//   1. Renderer 在 render() 里调用 writeCameraUbo() 写入 view/proj
//   2. Renderer 已 bind pipeline + mesh descriptor set（纹理 + UBO）
//   3. drawFloor() / drawSpotMeshes() 内部用 Push Constant 传各物体 model
//
// 【共享资源】
//   whiteTextureView_ + sampler_ 通过 bindResources() 写入各 Renderer 的 mesh descriptor
//
// =============================================================================

#include "VulkanContext.hpp"

#include <glm/glm.hpp>
#include <vector>

struct SceneObject {
    glm::mat4 model;
};

struct MeshGpu {
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory indexMemory = VK_NULL_HANDLE;
    uint32_t indexCount = 0;
};

class Scene {
public:
    bool init(VulkanContext &ctx);
    void shutdown();

    // 写入 mesh UBO 的 view/proj（model 走 Push Constant，见 drawMesh）
    void writeCameraUbo(void *meshUboMapped, const glm::mat4 &view, const glm::mat4 &proj) const;
    void drawFloor(VkCommandBuffer cmd, VkPipelineLayout layout, VkPipeline pipeline, VkDescriptorSet meshSet) const;
    void drawSpotMeshes(VkCommandBuffer cmd, VkPipelineLayout layout, VkPipeline pipeline, VkDescriptorSet meshSet) const;

    VkImageView whiteTextureView() const { return whiteTextureView_; }
    VkSampler sampler() const { return sampler_; }
    int objectCount() const { return static_cast<int>(objects_.size()) + 1; }

private:
    VulkanContext *ctx_ = nullptr;
    MeshGpu floor_;
    MeshGpu spot_;
    std::vector<SceneObject> objects_;

    VkImage whiteTexture_ = VK_NULL_HANDLE;
    VkDeviceMemory whiteTextureMemory_ = VK_NULL_HANDLE;
    VkImageView whiteTextureView_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;

    void drawMesh(VkCommandBuffer cmd, VkPipelineLayout layout, VkPipeline pipeline, VkDescriptorSet meshSet,
                  const MeshGpu &mesh, const glm::mat4 &model) const;
};

#endif
