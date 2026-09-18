/**
 * @file OitScene.cpp
 * @brief 场景资源加载 — Assimp 模型 + stb_image 纹理
 *
 * 加载流程：
 *   1. Assimp 读取 spot.obj / quad.obj（勿加 JoinIdenticalVertices，spot 模型会出问题）
 *   2. stb_image 读取 PNG 纹理；失败则 createWindowTexture 生成程序化窗格
 *   3. 组装 SceneObject 列表，供 StochasticDemo 逐 draw 渲染
 */

#include "OitScene.hpp"

#include "DemoRhi.hpp"
#include "VulkanBuffer.hpp"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <glm/gtc/matrix_transform.hpp>

#include <cstring>
#include <vector>

#include <stb_image.h>

namespace OitScene {
namespace {

struct Vertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 uv;
};

/** Staging → Device Local：顶点/索引缓冲上传 */
GpuMesh uploadMesh(VulkanContext &ctx, const std::vector<Vertex> &vertices, const std::vector<uint32_t> &indices) {
    GpuMesh mesh{};
    mesh.indexCount = static_cast<uint32_t>(indices.size());

    const VkDeviceSize vertexSize = sizeof(Vertex) * vertices.size();
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

    const VkDeviceSize indexSize = sizeof(uint32_t) * indices.size();
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

/** CPU 像素 → GPU RGBA8 纹理，布局最终为 SHADER_READ_ONLY_OPTIMAL */
void uploadTexturePixels(VulkanContext &ctx, GpuTexture &tex, const void *pixels, uint32_t width, uint32_t height) {
    const VkDeviceSize imageSize = static_cast<VkDeviceSize>(width) * height * 4;
    VulkanUtil::createImage(ctx, width, height, 1, VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_R8G8B8A8_UNORM,
                            VK_IMAGE_TILING_OPTIMAL,
                            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, tex.image, tex.memory);
    tex.view = VulkanUtil::createImageView(ctx, tex.image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, 1);

    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory stagingMem = VK_NULL_HANDLE;
    VulkanUtil::createBuffer(ctx, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging,
                             stagingMem);
    void *mapped = nullptr;
    vkMapMemory(ctx.device(), stagingMem, 0, imageSize, 0, &mapped);
    std::memcpy(mapped, pixels, imageSize);
    vkUnmapMemory(ctx.device(), stagingMem);

    VkCommandBuffer cmd = DemoRhi::beginSingleTimeCommands(ctx);
    DemoRhi::cmdTransitionImage(cmd, tex.image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_UNDEFINED,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {width, height, 1};
    vkCmdCopyBufferToImage(cmd, staging, tex.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    DemoRhi::cmdTransitionImage(cmd, tex.image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    DemoRhi::endSingleTimeCommands(ctx, cmd);

    vkDestroyBuffer(ctx.device(), staging, nullptr);
    vkFreeMemory(ctx.device(), stagingMem, nullptr);
}

} // namespace

glm::mat4 Scene::modelMatrix(const glm::vec3 &translation, float scale) {
    glm::mat4 model(1.0f);
    model = glm::translate(model, translation);
    return glm::scale(model, glm::vec3(scale));
}

GpuMesh Scene::loadModel(const std::string &relativePath) {
    const std::string fullPath = AppConfig::resourcePath(relativePath);
    Assimp::Importer importer;
    // 与 Vulkan_Rendering_Paths / OpenGL 相同 flags（勿加 JoinIdenticalVertices）
    const aiScene *scene = importer.ReadFile(
        fullPath, aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_FlipUVs | aiProcess_CalcTangentSpace);
    if (!scene || !scene->mRootNode || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE)) {
        return {};
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
    return uploadMesh(*ctx_, vertices, indices);
}

GpuTexture Scene::loadTextureFile(const std::string &relativePath) {
    GpuTexture tex{};
    const std::string fullPath = AppConfig::resourcePath(relativePath);
    int width = 0, height = 0, channels = 0;
    stbi_uc *pixels = stbi_load(fullPath.c_str(), &width, &height, &channels, STBI_rgb_alpha);
    if (!pixels) {
        return tex;
    }
    uploadTexturePixels(*ctx_, tex, pixels, static_cast<uint32_t>(width), static_cast<uint32_t>(height));
    stbi_image_free(pixels);
    return tex;
}

/**
 * 程序化窗格纹理：中心 alpha 较低（半透明），边缘 alpha → 1（窗框不透明）。
 * PNG 缺失时的回退方案。
 */
GpuTexture Scene::createWindowTexture(const glm::vec3 &tint, float centerAlpha) {
    constexpr int kSize = 64;
    std::vector<unsigned char> pixels(kSize * kSize * 4);
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(kSize);
            const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(kSize);
            const float edge = glm::min(glm::min(u, 1.0f - u), glm::min(v, 1.0f - v));
            const float alpha = glm::mix(centerAlpha, 1.0f, glm::smoothstep(0.05f, 0.18f, edge));
            const size_t idx = (static_cast<size_t>(y) * kSize + x) * 4;
            pixels[idx + 0] = static_cast<unsigned char>(glm::clamp(tint.r * 255.0f, 0.0f, 255.0f));
            pixels[idx + 1] = static_cast<unsigned char>(glm::clamp(tint.g * 255.0f, 0.0f, 255.0f));
            pixels[idx + 2] = static_cast<unsigned char>(glm::clamp(tint.b * 255.0f, 0.0f, 255.0f));
            pixels[idx + 3] = static_cast<unsigned char>(glm::clamp(alpha * 255.0f, 0.0f, 255.0f));
        }
    }
    GpuTexture tex{};
    uploadTexturePixels(*ctx_, tex, pixels.data(), kSize, kSize);
    return tex;
}

bool Scene::init(VulkanContext &ctx) {
    ctx_ = &ctx;
    quadMesh_ = loadModel("models/quad/quad.obj");
    spotMesh_ = loadModel("models/spot/spot.obj");
    if (quadMesh_.indexCount == 0 || spotMesh_.indexCount == 0) {
        return false;
    }

    texSpot_ = loadTextureFile("models/spot/spot.png");
    texWindowR_ = loadTextureFile("models/quad/window-r.png");
    texWindowG_ = loadTextureFile("models/quad/window-g.png");
    texWindowB_ = loadTextureFile("models/quad/window-b.png");

    if (!texSpot_.view) {
        texSpot_ = createWindowTexture(glm::vec3(0.75f, 0.75f, 0.78f), 1.0f);
    }
    if (!texWindowR_.view) {
        texWindowR_ = createWindowTexture(glm::vec3(1.0f, 0.25f, 0.25f), 0.35f);
    }
    if (!texWindowG_.view) {
        texWindowG_ = createWindowTexture(glm::vec3(0.25f, 0.95f, 0.35f), 0.35f);
    }
    if (!texWindowB_.view) {
        texWindowB_ = createWindowTexture(glm::vec3(0.25f, 0.55f, 1.0f), 0.35f);
    }

    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    vkCreateSampler(ctx.device(), &samplerInfo, nullptr, &sampler_);

    // 与 OpenGL StochasticTransparencyApp::renderScene 绘制顺序/布局一致
    objects_ = {
        {spotMesh_, modelMatrix(glm::vec3(0.0f, 0.0f, 0.0f)), texSpot_.view},           // 1. 不透明 spot
        {quadMesh_, modelMatrix(glm::vec3(0.3f, -0.1f, -0.8f)), texWindowB_.view},     // 2. 蓝色窗
        {quadMesh_, modelMatrix(glm::vec3(0.6f, 0.6f, -0.6f)), texWindowG_.view},      // 3. 绿色窗
        {quadMesh_, modelMatrix(glm::vec3(0.0f, 0.0f, 0.0f)), texWindowR_.view},        // 4. 红色窗
    };
    return true;
}

void Scene::destroyMesh(GpuMesh &mesh) {
    if (!ctx_) {
        return;
    }
    if (mesh.indexBuffer) {
        vkDestroyBuffer(ctx_->device(), mesh.indexBuffer, nullptr);
        vkFreeMemory(ctx_->device(), mesh.indexMemory, nullptr);
    }
    if (mesh.vertexBuffer) {
        vkDestroyBuffer(ctx_->device(), mesh.vertexBuffer, nullptr);
        vkFreeMemory(ctx_->device(), mesh.vertexMemory, nullptr);
    }
    mesh = {};
}

void Scene::destroyTexture(GpuTexture &tex) {
    if (!ctx_) {
        return;
    }
    if (tex.view) {
        vkDestroyImageView(ctx_->device(), tex.view, nullptr);
        vkDestroyImage(ctx_->device(), tex.image, nullptr);
        vkFreeMemory(ctx_->device(), tex.memory, nullptr);
    }
    tex = {};
}

void Scene::shutdown() {
    if (!ctx_) {
        return;
    }
    objects_.clear();
    destroyMesh(quadMesh_);
    destroyMesh(spotMesh_);
    destroyTexture(texSpot_);
    destroyTexture(texWindowR_);
    destroyTexture(texWindowG_);
    destroyTexture(texWindowB_);
    if (sampler_) {
        vkDestroySampler(ctx_->device(), sampler_, nullptr);
        sampler_ = VK_NULL_HANDLE;
    }
    ctx_ = nullptr;
}

} // namespace OitScene
