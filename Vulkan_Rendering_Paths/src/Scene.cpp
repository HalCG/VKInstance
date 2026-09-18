// =============================================================================
// Scene 实现 — 网格上传、纹理、绘制封装
// =============================================================================
//
// 【场景内容】
//   floor_    16×16 水平地面（y=0，法线朝上）
//   spot_     Assimp 加载 models/spot/spot.obj（失败则回退为立方体）
//   objects_  12 个 spot 实例，网格排列，每实例 model 矩阵不同
//
// 【顶点格式】 stride = 32 字节，与 mesh.vert / Pipeline 顶点属性一致：
//   offset 0:  vec3 pos
//   offset 12: vec3 normal
//   offset 24: vec2 uv
//
// 【上传路径 — Vulkan 典型 Staging 流程】
//   1. HOST_VISIBLE Staging Buffer ← memcpy CPU 顶点/索引
//   2. vkCmdCopyBuffer → DEVICE_LOCAL Vertex/Index Buffer（GPU 只读，快）
//   3. 销毁 Staging（init 时一次性完成，非每帧）
//
// 【为何 model 走 Push Constant 而非 UBO】
//   mesh UBO 只存 view/proj（所有 draw 共享）。
//   每个物体 model 不同：13 次 draw（1 地面 + 12 spot）若写 UBO 需每 draw 覆写；
//   Push Constant 专为「小数据、每 draw 变化」设计，见 vkCmdPushConstants。
//
// 【纹理】
//   spot.png → DEVICE_LOCAL Image，layout 最终为 SHADER_READ_ONLY_OPTIMAL
//   加载失败时 createWhiteTexture() 回退 1×1 白色
//
// =============================================================================

#include "Scene.hpp"
#include "AppConfig.hpp"
#include "VulkanBuffer.hpp"
#include "VulkanRhi.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cmath>
#include <cstring>
#include <vector>

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace {
struct Vertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 uv;
};

struct CameraUboData {
    glm::mat4 view;
    glm::mat4 proj;
};

VkVertexInputBindingDescription vertexBinding() {
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(Vertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return binding;
}

std::array<VkVertexInputAttributeDescription, 3> vertexAttributes() {
    std::array<VkVertexInputAttributeDescription, 3> attrs{};
    attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos)};
    attrs[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)};
    attrs[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)};
    return attrs;
}

// 将 CPU 顶点和索引上传到 GPU 本地缓冲，返回可供 vkCmdDrawIndexed 使用的 MeshGpu
MeshGpu uploadMesh(VulkanContext &ctx, const std::vector<Vertex> &vertices, const std::vector<uint32_t> &indices) {
    MeshGpu mesh{};
    mesh.indexCount = static_cast<uint32_t>(indices.size());

    // --- 顶点缓冲：Staging → Device Local ---
    VkDeviceSize vertexSize = sizeof(Vertex) * vertices.size();
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory stagingMem = VK_NULL_HANDLE;
    VulkanUtil::createBuffer(ctx, vertexSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging,
                               stagingMem);
    void *mapped = nullptr;
    vkMapMemory(ctx.device(), stagingMem, 0, vertexSize, 0, &mapped);
    std::memcpy(mapped, vertices.data(), vertexSize);
    vkUnmapMemory(ctx.device(), stagingMem);

    VulkanUtil::createBuffer(ctx, vertexSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mesh.vertexBuffer, mesh.vertexMemory);
    VulkanUtil::copyBuffer(ctx, staging, mesh.vertexBuffer, vertexSize);
    vkDestroyBuffer(ctx.device(), staging, nullptr);
    vkFreeMemory(ctx.device(), stagingMem, nullptr);

    // --- 索引缓冲：同样 Staging → Device Local（UINT32 索引）---
    VkDeviceSize indexSize = sizeof(uint32_t) * indices.size();
    VulkanUtil::createBuffer(ctx, indexSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging,
                               stagingMem);
    vkMapMemory(ctx.device(), stagingMem, 0, indexSize, 0, &mapped);
    std::memcpy(mapped, indices.data(), indexSize);
    vkUnmapMemory(ctx.device(), stagingMem);

    VulkanUtil::createBuffer(ctx, indexSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mesh.indexBuffer, mesh.indexMemory);
    VulkanUtil::copyBuffer(ctx, staging, mesh.indexBuffer, indexSize);
    vkDestroyBuffer(ctx.device(), staging, nullptr);
    vkFreeMemory(ctx.device(), stagingMem, nullptr);

    return mesh;
}

MeshGpu createCube(VulkanContext &ctx, float halfExtent) {
    const float h = halfExtent;
    const std::vector<Vertex> vertices = {
        {{-h, -h, h}, {0, 0, 1}, {0, 0}}, {{h, -h, h}, {0, 0, 1}, {1, 0}}, {{h, h, h}, {0, 0, 1}, {1, 1}},
        {{-h, h, h}, {0, 0, 1}, {0, 1}}, {{-h, -h, -h}, {0, 0, -1}, {1, 0}}, {{h, -h, -h}, {0, 0, -1}, {0, 0}},
        {{h, h, -h}, {0, 0, -1}, {0, 1}}, {{-h, h, -h}, {0, 0, -1}, {1, 1}},
    };
    const std::vector<uint32_t> indices = {
        0, 1, 2, 2, 3, 0, 1, 5, 6, 6, 2, 1, 5, 4, 7, 7, 6, 5,
        4, 0, 3, 3, 7, 4, 3, 2, 6, 6, 7, 3, 4, 5, 1, 1, 0, 4,
    };
    return uploadMesh(ctx, vertices, indices);
}

MeshGpu loadModelMesh(VulkanContext &ctx, const std::string &relativePath) {
    const std::string fullPath = AppConfig::resourcePath(relativePath);
    Assimp::Importer importer;
    const aiScene *scene = importer.ReadFile(
        fullPath, aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_FlipUVs | aiProcess_CalcTangentSpace);
    if (!scene || !scene->mRootNode || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE)) {
        return createCube(ctx, 0.6f);
    }

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    for (unsigned int m = 0; m < scene->mNumMeshes; ++m) {
        const aiMesh *mesh = scene->mMeshes[m];
        const uint32_t vertexOffset = static_cast<uint32_t>(vertices.size());

        for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
            Vertex v{};
            v.pos = glm::vec3(mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z);
            if (mesh->HasNormals()) {
                v.normal = glm::vec3(mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z);
            }
            if (mesh->mTextureCoords[0]) {
                v.uv = glm::vec2(mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y);
            }
            vertices.push_back(v);
        }

        for (unsigned int f = 0; f < mesh->mNumFaces; ++f) {
            const aiFace &face = mesh->mFaces[f];
            for (unsigned int j = 0; j < face.mNumIndices; ++j) {
                indices.push_back(vertexOffset + face.mIndices[j]);
            }
        }
    }

    return uploadMesh(ctx, vertices, indices);
}

MeshGpu createFloor(VulkanContext &ctx) {
    const std::vector<Vertex> vertices = {
        {{-8, 0, -8}, {0, 1, 0}, {0, 0}}, {{8, 0, -8}, {0, 1, 0}, {8, 0}},
        {{8, 0, 8}, {0, 1, 0}, {8, 8}}, {{-8, 0, 8}, {0, 1, 0}, {0, 8}},
    };
    const std::vector<uint32_t> indices = {0, 1, 2, 0, 2, 3};
    return uploadMesh(ctx, vertices, indices);
}

// 1×1 白色纹理：地面或模型贴图加载失败时的回退
void createWhiteTexture(VulkanContext &ctx, VkImage &image, VkDeviceMemory &memory, VkImageView &view) {
    VulkanUtil::createImage(ctx, 1, 1, 1, VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL,
                            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, image, memory);
    view = VulkanUtil::createImageView(ctx, image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, 1);

    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory stagingMem = VK_NULL_HANDLE;
    VulkanUtil::createBuffer(ctx, 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging,
                               stagingMem);
    void *mapped = nullptr;
    vkMapMemory(ctx.device(), stagingMem, 0, 4, 0, &mapped);
    const unsigned char white[] = {255, 255, 255, 255};
    std::memcpy(mapped, white, 4);
    vkUnmapMemory(ctx.device(), stagingMem);

    VkCommandBuffer cmd = VulkanRhi::beginSingleTimeCommands(ctx);
    VulkanRhi::cmdTransitionImage(cmd, image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_UNDEFINED,
                                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {1, 1, 1};
    vkCmdCopyBufferToImage(cmd, staging, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    VulkanRhi::cmdTransitionImage(cmd, image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    VulkanRhi::endSingleTimeCommands(ctx, cmd);

    vkDestroyBuffer(ctx.device(), staging, nullptr);
    vkFreeMemory(ctx.device(), stagingMem, nullptr);
}

// stb_image 加载 PNG → Staging → vkCmdCopyBufferToImage → SHADER_READ_ONLY_OPTIMAL
void createTextureFromFile(VulkanContext &ctx, const std::string &relativePath, VkImage &image, VkDeviceMemory &memory,
                            VkImageView &view) {
    const std::string fullPath = AppConfig::resourcePath(relativePath);
    int width = 0, height = 0, channels = 0;
    stbi_uc *pixels = stbi_load(fullPath.c_str(), &width, &height, &channels, STBI_rgb_alpha);
    if (!pixels) {
        createWhiteTexture(ctx, image, memory, view);
        return;
    }

    const VkDeviceSize imageSize = static_cast<VkDeviceSize>(width * height * 4);
    VulkanUtil::createImage(ctx, static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1, VK_SAMPLE_COUNT_1_BIT,
                            VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL,
                            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, image, memory);
    view = VulkanUtil::createImageView(ctx, image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, 1);

    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory stagingMem = VK_NULL_HANDLE;
    VulkanUtil::createBuffer(ctx, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging,
                             stagingMem);
    void *mapped = nullptr;
    vkMapMemory(ctx.device(), stagingMem, 0, imageSize, 0, &mapped);
    std::memcpy(mapped, pixels, imageSize);
    vkUnmapMemory(ctx.device(), stagingMem);
    stbi_image_free(pixels);

    VkCommandBuffer cmd = VulkanRhi::beginSingleTimeCommands(ctx);
    VulkanRhi::cmdTransitionImage(cmd, image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_UNDEFINED,
                                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
    vkCmdCopyBufferToImage(cmd, staging, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    VulkanRhi::cmdTransitionImage(cmd, image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    VulkanRhi::endSingleTimeCommands(ctx, cmd);

    vkDestroyBuffer(ctx.device(), staging, nullptr);
    vkFreeMemory(ctx.device(), stagingMem, nullptr);
}
} // namespace

bool Scene::init(VulkanContext &ctx) {
    ctx_ = &ctx;
    floor_ = createFloor(ctx);
    spot_ = loadModelMesh(ctx, "models/spot/spot.obj");

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    vkCreateSampler(ctx.device(), &samplerInfo, nullptr, &sampler_);

    createTextureFromFile(ctx, "models/spot/spot.png", whiteTexture_, whiteTextureMemory_, whiteTextureView_);

    // 在 xz 平面排列 kSpotInstanceCount 个 spot 实例（默认 3×4 网格）
    const int grid = static_cast<int>(std::sqrt(AppConfig::kSpotInstanceCount));
    objects_.clear();
    for (int z = 0; z < grid; ++z) {
        for (int x = 0; x < grid; ++x) {
            if (static_cast<int>(objects_.size()) >= AppConfig::kSpotInstanceCount) {
                break;
            }
            SceneObject obj;
            obj.model = glm::translate(glm::mat4(1.0f),
                                       glm::vec3((x - grid / 2) * AppConfig::kSpotGridSpacing, 0.0f,
                                                 (z - grid / 2) * AppConfig::kSpotGridSpacing));
            obj.model = glm::scale(obj.model, glm::vec3(AppConfig::kSpotScale));
            objects_.push_back(obj);
        }
    }
    return true;
}

void Scene::shutdown() {
    if (!ctx_) {
        return;
    }
    auto destroyMesh = [&](MeshGpu &mesh) {
        if (mesh.indexBuffer) {
            vkDestroyBuffer(ctx_->device(), mesh.indexBuffer, nullptr);
            vkFreeMemory(ctx_->device(), mesh.indexMemory, nullptr);
        }
        if (mesh.vertexBuffer) {
            vkDestroyBuffer(ctx_->device(), mesh.vertexBuffer, nullptr);
            vkFreeMemory(ctx_->device(), mesh.vertexMemory, nullptr);
        }
        mesh = {};
    };
    destroyMesh(floor_);
    destroyMesh(spot_);
    if (sampler_) {
        vkDestroySampler(ctx_->device(), sampler_, nullptr);
        sampler_ = VK_NULL_HANDLE;
    }
    if (whiteTextureView_) {
        vkDestroyImageView(ctx_->device(), whiteTextureView_, nullptr);
        vkDestroyImage(ctx_->device(), whiteTexture_, nullptr);
        vkFreeMemory(ctx_->device(), whiteTextureMemory_, nullptr);
        whiteTextureView_ = VK_NULL_HANDLE;
    }
    ctx_ = nullptr;
}

void Scene::writeCameraUbo(void *meshUboMapped, const glm::mat4 &view, const glm::mat4 &proj) const {
    const CameraUboData ubo{view, proj};
    std::memcpy(meshUboMapped, &ubo, sizeof(CameraUboData));
}

// 单次 Draw Call：Push Constant 传 model，避免多物体共享 UBO 时互相覆盖
void Scene::drawMesh(VkCommandBuffer cmd, VkPipelineLayout layout, VkPipeline /*pipeline*/, VkDescriptorSet /*meshSet*/,
                     const MeshGpu &mesh, const glm::mat4 &model) const {
    vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &model);
    VkBuffer vertexBuffers[] = {mesh.vertexBuffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(cmd, mesh.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, mesh.indexCount, 1, 0, 0, 0);
}

void Scene::drawFloor(VkCommandBuffer cmd, VkPipelineLayout layout, VkPipeline pipeline, VkDescriptorSet meshSet) const {
    drawMesh(cmd, layout, pipeline, meshSet, floor_, glm::mat4(1.0f));
}

void Scene::drawSpotMeshes(VkCommandBuffer cmd, VkPipelineLayout layout, VkPipeline pipeline,
                           VkDescriptorSet meshSet) const {
    for (const SceneObject &obj : objects_) {
        drawMesh(cmd, layout, pipeline, meshSet, spot_, obj.model);
    }
}
