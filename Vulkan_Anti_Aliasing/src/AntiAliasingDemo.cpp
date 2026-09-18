// AntiAliasingDemo 实现 — 类说明与四种 AA 模式见 AntiAliasingDemo.hpp
//
// 数据流概要：
//   None  → drawScene(swapchain RP, swapchain FB)
//   MSAA  → drawScene(msaa RP, msaa FB) — 附件含多采样颜色 + resolve 目标 + 多采样深度
//   FXAA  → drawScene(offscreen) → 全屏 fxaa.frag 采样 offscreenColor
//   TAA   → drawScene(offscreen, 抖动 proj) → taa.frag 混合 historyTex

// AntiAliasingDemo 实现 — 类说明与四种 AA 模式见 AntiAliasingDemo.hpp
//
// 数据流概要：
//   None  → drawScene(swapchain RP)
//   MSAA  → drawScene(msaa RP, multisample color → resolve 到 swapchain 图像)
//   FXAA  → drawScene(offscreen RP) → 全屏 fxaa.frag → swapchain
//   TAA   → drawScene(offscreen, 抖动 proj) → taa.frag(当前+历史+深度) → swapchain

#include "AntiAliasingDemo.hpp"
#include "DemoRhi.hpp"
#include "VulkanBuffer.hpp"

#include <GLFW/glfw3.h>

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace {
std::string resourcePath(const std::string &relative) {
    return "resources/" + relative;
}
struct Vertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 uv;
    glm::vec4 color;
};

struct SceneUbo {
    glm::mat4 view;
    glm::mat4 proj;
    glm::vec4 cameraPos;
    glm::vec4 lightDir;
};

struct PushConstants {
    glm::mat4 model;
    glm::vec4 colorTint;
    int useTexture;
    int padding[3];
};

struct TaaPushConstants {
    glm::mat4 invViewProj;
    glm::mat4 prevViewProj;
    glm::vec2 jitterOffset;
    float blendFactor;
    int firstFrame;
};

const char *modeName(int mode) {
    switch (mode) {
    case 0:
        return "None (1x Sample, No AA)";
    case 1:
        return "MSAA (Hardware Multisampling)";
    case 2:
        return "FXAA (Fast Approximate Anti-Aliasing)";
    case 3:
        return "TAA (Temporal Anti-Aliasing)";
    }
    return "?";
}

glm::vec2 getHaltonJitter(uint32_t index) {
    static const float haltonX[8] = {0.0f, -0.5f, 0.25f, -0.25f, 0.375f, -0.125f, 0.125f, -0.375f};
    static const float haltonY[8] = {0.0f, 0.333f, -0.333f, 0.111f, -0.111f, 0.444f, -0.444f, -0.222f};
    return glm::vec2(haltonX[index % 8], haltonY[index % 8]);
}

VkVertexInputBindingDescription vertexBinding() {
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(Vertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return binding;
}

std::array<VkVertexInputAttributeDescription, 4> vertexAttributes() {
    std::array<VkVertexInputAttributeDescription, 4> attrs{};
    attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos)};
    attrs[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)};
    attrs[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)};
    attrs[3] = {3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, color)};
    return attrs;
}

AntiAliasingDemo::GpuMesh uploadMesh(VulkanContext &ctx, const std::vector<Vertex> &vertices,
                                     const std::vector<uint32_t> &indices) {
    AntiAliasingDemo::GpuMesh mesh{};
    mesh.indexCount = static_cast<uint32_t>(indices.size());

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

AntiAliasingDemo::GpuMesh createFallbackCube(VulkanContext &ctx) {
    const float h = 0.6f;
    const std::vector<Vertex> vertices = {
        {{-h, -h, h}, {0, 0, 1}, {0, 0}, {1, 1, 1, 1}}, {{h, -h, h}, {0, 0, 1}, {1, 0}, {1, 1, 1, 1}},
        {{h, h, h}, {0, 0, 1}, {1, 1}, {1, 1, 1, 1}},  {{-h, h, h}, {0, 0, 1}, {0, 1}, {1, 1, 1, 1}},
        {{-h, -h, -h}, {0, 0, -1}, {1, 0}, {1, 1, 1, 1}}, {{h, -h, -h}, {0, 0, -1}, {0, 0}, {1, 1, 1, 1}},
        {{h, h, -h}, {0, 0, -1}, {0, 1}, {1, 1, 1, 1}}, {{-h, h, -h}, {0, 0, -1}, {1, 1}, {1, 1, 1, 1}},
    };
    const std::vector<uint32_t> indices = {
        0, 1, 2, 2, 3, 0, 1, 5, 6, 6, 2, 1, 5, 4, 7, 7, 6, 5,
        4, 0, 3, 3, 7, 4, 3, 2, 6, 6, 7, 3, 4, 5, 1, 1, 0, 4,
    };
    return uploadMesh(ctx, vertices, indices);
}

AntiAliasingDemo::GpuMesh loadSpotModel(VulkanContext &ctx) {
    const std::string fullPath = resourcePath("models/spot/spot.obj");
    Assimp::Importer importer;
    const aiScene *scene = importer.ReadFile(
        fullPath, aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_FlipUVs | aiProcess_CalcTangentSpace);
    if (!scene || !scene->mRootNode || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE)) {
        return createFallbackCube(ctx);
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
            } else {
                v.normal = glm::vec3(0, 1, 0);
            }
            if (mesh->mTextureCoords[0]) {
                v.uv = glm::vec2(mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y);
            } else {
                v.uv = glm::vec2(0, 0);
            }
            v.color = glm::vec4(1.0f);
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

AntiAliasingDemo::GpuMesh createFloorMesh(VulkanContext &ctx) {
    const float size = 14.0f;
    const float uvScale = 14.0f;
    const std::vector<Vertex> vertices = {
        {{-size, 0, -size}, {0, 1, 0}, {0, 0}, {1, 1, 1, 1}},
        {{size, 0, -size}, {0, 1, 0}, {uvScale, 0}, {1, 1, 1, 1}},
        {{size, 0, size}, {0, 1, 0}, {uvScale, uvScale}, {1, 1, 1, 1}},
        {{-size, 0, size}, {0, 1, 0}, {0, uvScale}, {1, 1, 1, 1}},
    };
    const std::vector<uint32_t> indices = {0, 1, 2, 0, 2, 3};
    return uploadMesh(ctx, vertices, indices);
}

AntiAliasingDemo::GpuMesh createSharpPyramids(VulkanContext &ctx) {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    auto addPyramid = [&](const glm::vec3 &baseCenter, float radius, float height, const glm::vec4 &color) {
        uint32_t start = static_cast<uint32_t>(vertices.size());
        glm::vec3 apex = baseCenter + glm::vec3(0, height, 0);
        glm::vec3 b0 = baseCenter + glm::vec3(-radius, 0, -radius);
        glm::vec3 b1 = baseCenter + glm::vec3(radius, 0, -radius);
        glm::vec3 b2 = baseCenter + glm::vec3(radius, 0, radius);
        glm::vec3 b3 = baseCenter + glm::vec3(-radius, 0, radius);

        auto addTri = [&](const glm::vec3 &p0, const glm::vec3 &p1, const glm::vec3 &p2) {
            glm::vec3 norm = glm::normalize(glm::cross(p1 - p0, p2 - p0));
            uint32_t idx = static_cast<uint32_t>(vertices.size());
            vertices.push_back({p0, norm, {0, 0}, color});
            vertices.push_back({p1, norm, {1, 0}, color});
            vertices.push_back({p2, norm, {0.5f, 1}, color});
            indices.push_back(idx);
            indices.push_back(idx + 1);
            indices.push_back(idx + 2);
        };

        addTri(b0, b1, apex);
        addTri(b1, b2, apex);
        addTri(b2, b3, apex);
        addTri(b3, b0, apex);
    };

    const std::array<glm::vec4, 6> colors = {
        glm::vec4(1.0f, 0.25f, 0.2f, 1.0f), glm::vec4(0.2f, 0.95f, 0.35f, 1.0f), glm::vec4(0.2f, 0.6f, 1.0f, 1.0f),
        glm::vec4(1.0f, 0.85f, 0.1f, 1.0f), glm::vec4(0.95f, 0.35f, 0.9f, 1.0f), glm::vec4(0.2f, 0.95f, 0.95f, 1.0f),
    };

    int colorIdx = 0;
    for (float x : {-6.0f, -2.0f, 2.0f, 6.0f}) {
        for (float z : {-5.0f, 0.0f, 5.0f}) {
            addPyramid(glm::vec3(x, 0.0f, z), 0.55f, 2.6f, colors[colorIdx % colors.size()]);
            colorIdx++;
        }
    }

    return uploadMesh(ctx, vertices, indices);
}

AntiAliasingDemo::GpuMesh createAASpikes(VulkanContext &ctx) {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    auto addCube = [&](const glm::vec3 &c, float h, const glm::vec4 &color) {
        uint32_t base = static_cast<uint32_t>(vertices.size());
        const std::array<glm::vec3, 8> positions = {
            c + glm::vec3(-h, -h, h), c + glm::vec3(h, -h, h), c + glm::vec3(h, h, h),  c + glm::vec3(-h, h, h),
            c + glm::vec3(-h, -h, -h), c + glm::vec3(h, -h, -h), c + glm::vec3(h, h, -h), c + glm::vec3(-h, h, -h),
        };
        const std::array<glm::vec3, 6> normals = {
            glm::vec3(0, 0, 1), glm::vec3(1, 0, 0), glm::vec3(0, 0, -1),
            glm::vec3(-1, 0, 0), glm::vec3(0, 1, 0), glm::vec3(0, -1, 0),
        };
        const std::array<std::array<int, 4>, 6> faces = {{
            {0, 1, 2, 3}, {1, 5, 6, 2}, {5, 4, 7, 6}, {4, 0, 3, 7}, {3, 2, 6, 7}, {4, 5, 1, 0}
        }};

        for (int f = 0; f < 6; ++f) {
            uint32_t idx = static_cast<uint32_t>(vertices.size());
            glm::vec3 n = normals[f];
            vertices.push_back({positions[faces[f][0]], n, {0, 0}, color});
            vertices.push_back({positions[faces[f][1]], n, {1, 0}, color});
            vertices.push_back({positions[faces[f][2]], n, {1, 1}, color});
            vertices.push_back({positions[faces[f][3]], n, {0, 1}, color});

            indices.push_back(idx);
            indices.push_back(idx + 1);
            indices.push_back(idx + 2);
            indices.push_back(idx);
            indices.push_back(idx + 2);
            indices.push_back(idx + 3);
        }
    };

    const std::array<glm::vec4, 5> colors = {
        glm::vec4(1.0f, 0.3f, 0.3f, 1.0f), glm::vec4(0.3f, 1.0f, 0.4f, 1.0f), glm::vec4(0.3f, 0.6f, 1.0f, 1.0f),
        glm::vec4(1.0f, 0.9f, 0.2f, 1.0f), glm::vec4(0.9f, 0.4f, 1.0f, 1.0f),
    };

    int idx = 0;
    for (int x = -3; x <= 3; ++x) {
        for (int z = -3; z <= 3; ++z) {
            if (std::abs(x) == 1 && std::abs(z) == 1) continue;
            glm::vec3 center(static_cast<float>(x) * 1.6f, 0.45f + 0.1f * static_cast<float>((x + z) % 3),
                             static_cast<float>(z) * 1.6f);
            addCube(center, 0.25f, colors[idx % colors.size()]);
            idx++;
        }
    }

    return uploadMesh(ctx, vertices, indices);
}

AntiAliasingDemo::GpuMesh createThinArches(VulkanContext &ctx) {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    auto addArch = [&](const glm::vec3 &center, float radius, float thickness, float height, const glm::vec4 &color) {
        const int segments = 24;
        for (int i = 0; i < segments; ++i) {
            float a0 = (static_cast<float>(i) / segments) * glm::pi<float>();
            float a1 = (static_cast<float>(i + 1) / segments) * glm::pi<float>();

            glm::vec3 p0_outer = center + glm::vec3(std::cos(a0) * radius, std::sin(a0) * height, 0);
            glm::vec3 p1_outer = center + glm::vec3(std::cos(a1) * radius, std::sin(a1) * height, 0);
            glm::vec3 p0_inner = center + glm::vec3(std::cos(a0) * (radius - thickness), std::sin(a0) * (height - thickness), 0);
            glm::vec3 p1_inner = center + glm::vec3(std::cos(a1) * (radius - thickness), std::sin(a1) * (height - thickness), 0);

            glm::vec3 norm(0, 0, 1);
            uint32_t idx = static_cast<uint32_t>(vertices.size());
            vertices.push_back({p0_inner, norm, {0, 0}, color});
            vertices.push_back({p0_outer, norm, {1, 0}, color});
            vertices.push_back({p1_outer, norm, {1, 1}, color});
            vertices.push_back({p1_inner, norm, {0, 1}, color});

            indices.push_back(idx);
            indices.push_back(idx + 1);
            indices.push_back(idx + 2);
            indices.push_back(idx);
            indices.push_back(idx + 2);
            indices.push_back(idx + 3);
        }
    };

    addArch(glm::vec3(-4.0f, 0.0f, 0.0f), 2.2f, 0.12f, 2.5f, glm::vec4(1.0f, 0.9f, 0.2f, 1.0f));
    addArch(glm::vec3(4.0f, 0.0f, 0.0f), 2.2f, 0.12f, 2.5f, glm::vec4(0.2f, 0.95f, 0.95f, 1.0f));
    addArch(glm::vec3(0.0f, 0.0f, -4.0f), 2.5f, 0.12f, 2.8f, glm::vec4(1.0f, 0.3f, 0.8f, 1.0f));

    return uploadMesh(ctx, vertices, indices);
}

void createTextureFromFile(VulkanContext &ctx, const std::string &relativePath, AntiAliasingDemo::GpuTexture &tex) {
    const std::string fullPath = resourcePath(relativePath);
    int width = 0, height = 0, channels = 0;
    bool loadedByStb = true;
    stbi_uc *pixels = stbi_load(fullPath.c_str(), &width, &height, &channels, STBI_rgb_alpha);
    if (!pixels) {
        loadedByStb = false;
        width = 1;
        height = 1;
        const unsigned char white[] = {255, 255, 255, 255};
        pixels = (stbi_uc *)malloc(4);
        std::memcpy(pixels, white, 4);
    }

    const VkDeviceSize imageSize = static_cast<VkDeviceSize>(width * height * 4);
    VulkanUtil::createImage(ctx, static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1, VK_SAMPLE_COUNT_1_BIT,
                            VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL,
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
    if (loadedByStb) {
        stbi_image_free(pixels);
    } else {
        free(pixels);
    }

    VkCommandBuffer cmd = DemoRhi::beginSingleTimeCommands(ctx);
    DemoRhi::cmdTransitionImage(cmd, tex.image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_UNDEFINED,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
    vkCmdCopyBufferToImage(cmd, staging, tex.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    DemoRhi::cmdTransitionImage(cmd, tex.image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    DemoRhi::endSingleTimeCommands(ctx, cmd);

    vkDestroyBuffer(ctx.device(), staging, nullptr);
    vkFreeMemory(ctx.device(), stagingMem, nullptr);
}

void createProceduralGridTexture(VulkanContext &ctx, AntiAliasingDemo::GpuTexture &tex) {
    const int w = 256;
    const int h = 256;
    std::vector<uint32_t> pixels(w * h);

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            bool border = (x < 6 || x > 249 || y < 6 || y > 249);
            bool check = ((x / 32) + (y / 32)) % 2 == 0;
            uint32_t c = border ? 0xFF333333 : (check ? 0xFFEAEAEA : 0xFF777777);
            pixels[y * w + x] = c;
        }
    }

    const VkDeviceSize imageSize = static_cast<VkDeviceSize>(w * h * 4);
    VulkanUtil::createImage(ctx, static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1, VK_SAMPLE_COUNT_1_BIT,
                            VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL,
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
    std::memcpy(mapped, pixels.data(), imageSize);
    vkUnmapMemory(ctx.device(), stagingMem);

    VkCommandBuffer cmd = DemoRhi::beginSingleTimeCommands(ctx);
    DemoRhi::cmdTransitionImage(cmd, tex.image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_UNDEFINED,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1};
    vkCmdCopyBufferToImage(cmd, staging, tex.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    DemoRhi::cmdTransitionImage(cmd, tex.image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    DemoRhi::endSingleTimeCommands(ctx, cmd);

    vkDestroyBuffer(ctx.device(), staging, nullptr);
    vkFreeMemory(ctx.device(), stagingMem, nullptr);
}

void createWhiteTexture(VulkanContext &ctx, AntiAliasingDemo::GpuTexture &tex) {
    VulkanUtil::createImage(ctx, 1, 1, 1, VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL,
                            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, tex.image, tex.memory);
    tex.view = VulkanUtil::createImageView(ctx, tex.image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, 1);

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

    VkCommandBuffer cmd = DemoRhi::beginSingleTimeCommands(ctx);
    DemoRhi::cmdTransitionImage(cmd, tex.image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_UNDEFINED,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {1, 1, 1};
    vkCmdCopyBufferToImage(cmd, staging, tex.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    DemoRhi::cmdTransitionImage(cmd, tex.image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    DemoRhi::endSingleTimeCommands(ctx, cmd);

    vkDestroyBuffer(ctx.device(), staging, nullptr);
    vkFreeMemory(ctx.device(), stagingMem, nullptr);
}

void writeMeshDescriptorSet(VulkanContext &ctx, VkDescriptorSet set, VkBuffer ubo, VkImageView texView,
                            VkSampler sampler) {
    VkDescriptorBufferInfo bufferInfo{ubo, 0, VK_WHOLE_SIZE};
    VkDescriptorImageInfo imageInfo{sampler, texView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

    VkWriteDescriptorSet writes[2]{};
    writes[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 0, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                 nullptr, &bufferInfo, nullptr};
    writes[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 1, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                 &imageInfo, nullptr, nullptr};
    vkUpdateDescriptorSets(ctx.device(), 2, writes, 0, nullptr);
}

void destroyMesh(VulkanContext &ctx, AntiAliasingDemo::GpuMesh &mesh) {
    if (mesh.vertexBuffer) {
        vkDestroyBuffer(ctx.device(), mesh.vertexBuffer, nullptr);
        vkFreeMemory(ctx.device(), mesh.vertexMemory, nullptr);
    }
    if (mesh.indexBuffer) {
        vkDestroyBuffer(ctx.device(), mesh.indexBuffer, nullptr);
        vkFreeMemory(ctx.device(), mesh.indexMemory, nullptr);
    }
    mesh = {};
}

void destroyTexture(VulkanContext &ctx, AntiAliasingDemo::GpuTexture &tex) {
    if (tex.view) {
        vkDestroyImageView(ctx.device(), tex.view, nullptr);
    }
    if (tex.image) {
        vkDestroyImage(ctx.device(), tex.image, nullptr);
        vkFreeMemory(ctx.device(), tex.memory, nullptr);
    }
    tex = {};
}

void writeTaaDescriptorSet(VulkanContext &ctx, VkDescriptorSet set, VkImageView currentView, VkImageView historyView,
                           VkImageView depthView, VkSampler sampler) {
    VkDescriptorImageInfo currentInfo{sampler, currentView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorImageInfo historyInfo{sampler, historyView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorImageInfo depthInfo{sampler, depthView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

    VkWriteDescriptorSet writes[3]{};
    writes[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 0, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &currentInfo, nullptr, nullptr};
    writes[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 1, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &historyInfo, nullptr, nullptr};
    writes[2] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 2, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &depthInfo, nullptr, nullptr};
    vkUpdateDescriptorSets(ctx.device(), 3, writes, 0, nullptr);
}

} // namespace

AntiAliasingDemo::AntiAliasingDemo(DemoApp &app) : app_(app) {}

AntiAliasingDemo::~AntiAliasingDemo() { shutdown(); }

void AntiAliasingDemo::init() {
    auto &ctx = app_.ctx();
    msaaSamples_ = DemoRhi::getMaxUsableSampleCount(ctx);

    createSceneGeometry();

    VulkanUtil::createBuffer(ctx, sizeof(SceneUbo), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, ubo_,
                             uboMemory_);
    vkMapMemory(ctx.device(), uboMemory_, 0, sizeof(SceneUbo), 0, &uboMapped_);

    VkDescriptorSetLayoutBinding bindings[2]{};
    bindings[0] = {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    bindings[1] = {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};

    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = 2;
    layoutInfo.pBindings = bindings;
    vkCreateDescriptorSetLayout(ctx.device(), &layoutInfo, nullptr, &meshSetLayout_);

    fxaaSetLayout_ = DemoRhi::createTextureDescriptorLayout(ctx);

    VkDescriptorSetLayoutBinding taaBindings[3]{};
    taaBindings[0] = {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    taaBindings[1] = {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    taaBindings[2] = {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};

    VkDescriptorSetLayoutCreateInfo taaLayoutInfoBinding{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    taaLayoutInfoBinding.bindingCount = 3;
    taaLayoutInfoBinding.pBindings = taaBindings;
    vkCreateDescriptorSetLayout(ctx.device(), &taaLayoutInfoBinding, nullptr, &taaSetLayout_);

    pool_ = DemoRhi::createDescriptorPool(ctx, 16);

    spotSet_ = DemoRhi::allocateSet(ctx, pool_, meshSetLayout_);
    gridSet_ = DemoRhi::allocateSet(ctx, pool_, meshSetLayout_);
    colorSet_ = DemoRhi::allocateSet(ctx, pool_, meshSetLayout_);
    fxaaSet_ = DemoRhi::allocateSet(ctx, pool_, fxaaSetLayout_);
    taaSet_[0] = DemoRhi::allocateSet(ctx, pool_, taaSetLayout_);
    taaSet_[1] = DemoRhi::allocateSet(ctx, pool_, taaSetLayout_);

    sampler_ = DemoRhi::createLinearSampler(ctx);

    writeMeshDescriptorSet(ctx, spotSet_, ubo_, spotTex_.view, sampler_);
    writeMeshDescriptorSet(ctx, gridSet_, ubo_, gridTex_.view, sampler_);
    writeMeshDescriptorSet(ctx, colorSet_, ubo_, whiteTex_.view, sampler_);

    meshVert_ = DemoRhi::loadSpirv(ctx, "resources/shaders/mesh_msaa_vert.spv");
    meshFrag_ = DemoRhi::loadSpirv(ctx, "resources/shaders/mesh_msaa_frag.spv");
    fxaaVert_ = DemoRhi::loadSpirv(ctx, "resources/shaders/fxaa_vert.spv");
    fxaaFrag_ = DemoRhi::loadSpirv(ctx, "resources/shaders/fxaa_frag.spv");
    taaVert_ = DemoRhi::loadSpirv(ctx, "resources/shaders/taa_vert.spv");
    taaFrag_ = DemoRhi::loadSpirv(ctx, "resources/shaders/taa_frag.spv");

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.size = sizeof(PushConstants);

    VkPipelineLayoutCreateInfo meshLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    meshLayoutInfo.setLayoutCount = 1;
    meshLayoutInfo.pSetLayouts = &meshSetLayout_;
    meshLayoutInfo.pushConstantRangeCount = 1;
    meshLayoutInfo.pPushConstantRanges = &pushRange;
    vkCreatePipelineLayout(ctx.device(), &meshLayoutInfo, nullptr, &meshLayout_);

    VkPipelineLayoutCreateInfo fxaaLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    fxaaLayoutInfo.setLayoutCount = 1;
    fxaaLayoutInfo.pSetLayouts = &fxaaSetLayout_;
    vkCreatePipelineLayout(ctx.device(), &fxaaLayoutInfo, nullptr, &fxaaLayout_);

    VkPushConstantRange taaPushRange{};
    taaPushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    taaPushRange.size = sizeof(TaaPushConstants);

    VkPipelineLayoutCreateInfo taaLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    taaLayoutInfo.setLayoutCount = 1;
    taaLayoutInfo.pSetLayouts = &taaSetLayout_;
    taaLayoutInfo.pushConstantRangeCount = 1;
    taaLayoutInfo.pPushConstantRanges = &taaPushRange;
    vkCreatePipelineLayout(ctx.device(), &taaLayoutInfo, nullptr, &taaLayout_);

    createOffscreenTargets();
    createMsaaTargets();
    createTaaTargets();
    createPipelines();
    updateTitle();
}

void AntiAliasingDemo::createSceneGeometry() {
    auto &ctx = app_.ctx();
    spotMesh_ = loadSpotModel(ctx);
    floorMesh_ = createFloorMesh(ctx);
    pyramidMesh_ = createSharpPyramids(ctx);
    spikesMesh_ = createAASpikes(ctx);
    archesMesh_ = createThinArches(ctx);

    createTextureFromFile(ctx, "models/spot/spot.png", spotTex_);
    createProceduralGridTexture(ctx, gridTex_);
    createWhiteTexture(ctx, whiteTex_);
}

void AntiAliasingDemo::destroySceneGeometry() {
    auto &ctx = app_.ctx();
    destroyMesh(ctx, spotMesh_);
    destroyMesh(ctx, floorMesh_);
    destroyMesh(ctx, pyramidMesh_);
    destroyMesh(ctx, spikesMesh_);
    destroyMesh(ctx, archesMesh_);

    destroyTexture(ctx, spotTex_);
    destroyTexture(ctx, gridTex_);
    destroyTexture(ctx, whiteTex_);
}

void AntiAliasingDemo::shutdown() {
    auto &ctx = app_.ctx();
    vkDeviceWaitIdle(ctx.device());
    destroyPipelines();
    destroyTaaTargets();
    destroyMsaaTargets();
    destroyOffscreenTargets();
    destroySceneGeometry();

    if (offscreenRp_) {
        vkDestroyRenderPass(ctx.device(), offscreenRp_, nullptr);
        offscreenRp_ = VK_NULL_HANDLE;
    }
    if (msaaRp_) {
        vkDestroyRenderPass(ctx.device(), msaaRp_, nullptr);
        msaaRp_ = VK_NULL_HANDLE;
    }

    if (taaLayout_) {
        vkDestroyPipelineLayout(ctx.device(), taaLayout_, nullptr);
    }
    if (fxaaLayout_) {
        vkDestroyPipelineLayout(ctx.device(), fxaaLayout_, nullptr);
    }
    if (meshLayout_) {
        vkDestroyPipelineLayout(ctx.device(), meshLayout_, nullptr);
    }
    if (taaFrag_) {
        vkDestroyShaderModule(ctx.device(), taaFrag_, nullptr);
    }
    if (taaVert_) {
        vkDestroyShaderModule(ctx.device(), taaVert_, nullptr);
    }
    if (fxaaFrag_) {
        vkDestroyShaderModule(ctx.device(), fxaaFrag_, nullptr);
    }
    if (fxaaVert_) {
        vkDestroyShaderModule(ctx.device(), fxaaVert_, nullptr);
    }
    if (meshFrag_) {
        vkDestroyShaderModule(ctx.device(), meshFrag_, nullptr);
    }
    if (meshVert_) {
        vkDestroyShaderModule(ctx.device(), meshVert_, nullptr);
    }
    if (sampler_) {
        vkDestroySampler(ctx.device(), sampler_, nullptr);
    }
    if (pool_) {
        vkDestroyDescriptorPool(ctx.device(), pool_, nullptr);
    }
    if (taaSetLayout_) {
        vkDestroyDescriptorSetLayout(ctx.device(), taaSetLayout_, nullptr);
    }
    if (fxaaSetLayout_) {
        vkDestroyDescriptorSetLayout(ctx.device(), fxaaSetLayout_, nullptr);
    }
    if (meshSetLayout_) {
        vkDestroyDescriptorSetLayout(ctx.device(), meshSetLayout_, nullptr);
    }
    if (uboMapped_) {
        vkUnmapMemory(ctx.device(), uboMemory_);
        uboMapped_ = nullptr;
    }
    if (ubo_) {
        vkDestroyBuffer(ctx.device(), ubo_, nullptr);
        vkFreeMemory(ctx.device(), uboMemory_, nullptr);
    }
    meshSetLayout_ = VK_NULL_HANDLE;
    fxaaSetLayout_ = VK_NULL_HANDLE;
    taaSetLayout_ = VK_NULL_HANDLE;
    pool_ = VK_NULL_HANDLE;
    spotSet_ = VK_NULL_HANDLE;
    gridSet_ = VK_NULL_HANDLE;
    colorSet_ = VK_NULL_HANDLE;
    fxaaSet_ = VK_NULL_HANDLE;
    taaSet_[0] = VK_NULL_HANDLE;
    taaSet_[1] = VK_NULL_HANDLE;
    meshLayout_ = VK_NULL_HANDLE;
    fxaaLayout_ = VK_NULL_HANDLE;
    taaLayout_ = VK_NULL_HANDLE;
}

void AntiAliasingDemo::onResize() {
    auto &ctx = app_.ctx();
    const auto extent = ctx.swapChainExtent();
    std::cout << "[Demo Log] Resizing AA Targets to " << extent.width << "x" << extent.height << "..." << std::endl;
    vkDeviceWaitIdle(ctx.device());
    destroyTaaTargets();
    destroyMsaaTargets();
    destroyOffscreenTargets();
    createOffscreenTargets();
    createMsaaTargets();
    createTaaTargets();
    taaFrameCount_ = 0;
    if (mode_ == AntiAliasingDemo::Mode::FXAA) {
        DemoRhi::writeTextureDescriptor(ctx, fxaaSet_, offscreenColorView_, sampler_);
    }
}

void AntiAliasingDemo::onKey(int key, int action) {
    if (action != GLFW_PRESS) {
        return;
    }
    if (key == GLFW_KEY_1) {
        mode_ = Mode::None;
    } else if (key == GLFW_KEY_2) {
        mode_ = Mode::MSAA;
    } else if (key == GLFW_KEY_3) {
        mode_ = Mode::FXAA;
        DemoRhi::writeTextureDescriptor(app_.ctx(), fxaaSet_, offscreenColorView_, sampler_);
    } else if (key == GLFW_KEY_4) {
        mode_ = Mode::TAA;
        taaFrameCount_ = 0;
    }
    std::cout << "[Demo Log] Key Event: Mode changed to " << modeName(static_cast<int>(mode_)) << std::endl;
    updateTitle();
}

void AntiAliasingDemo::updateTitle() {
    std::string title = "Vulkan Anti-Aliasing Demo | Mode: ";
    title += modeName(static_cast<int>(mode_));
    title += " | [Press 1=None, 2=MSAA, 3=FXAA, 4=TAA]";
    glfwSetWindowTitle(app_.ctx().window(), title.c_str());
}

// 离屏 RenderPass：颜色 STORE + SAMPLED（供 FXAA/TAA 全屏 Pass 读取），深度仅几何测试
// 离屏 RenderPass：颜色 STORE + SAMPLED（供 FXAA/TAA 片元着色器读取）
void AntiAliasingDemo::createOffscreenTargets() {
    auto &ctx = app_.ctx();
    const auto extent = ctx.swapChainExtent();
    if (!offscreenRp_) {
        offscreenRp_ = DemoRhi::createOffscreenRenderPass(ctx, ctx.swapChainImageFormat());
    }

    VulkanUtil::createImage(ctx, extent.width, extent.height, 1, VK_SAMPLE_COUNT_1_BIT, ctx.swapChainImageFormat(),
                            VK_IMAGE_TILING_OPTIMAL,
                            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, offscreenColor_, offscreenColorMem_);
    offscreenColorView_ = VulkanUtil::createImageView(ctx, offscreenColor_, ctx.swapChainImageFormat(),
                                                      VK_IMAGE_ASPECT_COLOR_BIT, 1);

    VulkanUtil::createImage(ctx, extent.width, extent.height, 1, VK_SAMPLE_COUNT_1_BIT,
                            VulkanUtil::findDepthFormat(ctx), VK_IMAGE_TILING_OPTIMAL,
                            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                            offscreenDepth_, offscreenDepthMem_);
    offscreenDepthView_ = VulkanUtil::createImageView(ctx, offscreenDepth_, VulkanUtil::findDepthFormat(ctx),
                                                      VK_IMAGE_ASPECT_DEPTH_BIT, 1);

    offscreenFb_ = DemoRhi::createFramebuffer(ctx, offscreenRp_, offscreenColorView_, offscreenDepthView_, extent);
}

void AntiAliasingDemo::destroyOffscreenTargets() {
    auto &ctx = app_.ctx();
    if (offscreenFb_) {
        vkDestroyFramebuffer(ctx.device(), offscreenFb_, nullptr);
        offscreenFb_ = VK_NULL_HANDLE;
    }
    if (offscreenDepthView_) {
        vkDestroyImageView(ctx.device(), offscreenDepthView_, nullptr);
        offscreenDepthView_ = VK_NULL_HANDLE;
    }
    if (offscreenDepth_) {
        vkDestroyImage(ctx.device(), offscreenDepth_, nullptr);
        vkFreeMemory(ctx.device(), offscreenDepthMem_, nullptr);
        offscreenDepth_ = VK_NULL_HANDLE;
        offscreenDepthMem_ = VK_NULL_HANDLE;
    }
    if (offscreenColorView_) {
        vkDestroyImageView(ctx.device(), offscreenColorView_, nullptr);
        offscreenColorView_ = VK_NULL_HANDLE;
    }
    if (offscreenColor_) {
        vkDestroyImage(ctx.device(), offscreenColor_, nullptr);
        vkFreeMemory(ctx.device(), offscreenColorMem_, nullptr);
        offscreenColor_ = VK_NULL_HANDLE;
        offscreenColorMem_ = VK_NULL_HANDLE;
    }
}

// MSAA 附件：多采样颜色（TRANSIENT，仅 MSAA 缓冲）+ Resolve 到 Swapchain 图像 + 多采样深度
// MSAA RenderPass：附件0=多采样颜色(不存储) 附件1=Resolve单采样(Swapchain图) 附件2=多采样深度
void AntiAliasingDemo::createMsaaTargets() {
    auto &ctx = app_.ctx();
    const auto extent = ctx.swapChainExtent();
    if (msaaSamples_ == VK_SAMPLE_COUNT_1_BIT) {
        return;
    }

    if (!msaaRp_) {
        msaaRp_ = DemoRhi::createMsaaRenderPass(ctx, msaaSamples_);
    }
    VulkanUtil::createImage(ctx, extent.width, extent.height, 1, msaaSamples_, ctx.swapChainImageFormat(),
                            VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, msaaColor_, msaaColorMem_);
    msaaColorView_ = VulkanUtil::createImageView(ctx, msaaColor_, ctx.swapChainImageFormat(), VK_IMAGE_ASPECT_COLOR_BIT, 1);

    VulkanUtil::createImage(ctx, extent.width, extent.height, 1, msaaSamples_, VulkanUtil::findDepthFormat(ctx),
                            VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, msaaDepth_, msaaDepthMem_);
    msaaDepthView_ = VulkanUtil::createImageView(ctx, msaaDepth_, VulkanUtil::findDepthFormat(ctx),
                                                 VK_IMAGE_ASPECT_DEPTH_BIT, 1);

    msaaFbs_.resize(ctx.swapChainImageViews().size());
    for (size_t i = 0; i < ctx.swapChainImageViews().size(); ++i) {
        msaaFbs_[i] = DemoRhi::createMsaaFramebuffer(ctx, msaaRp_, msaaColorView_, ctx.swapChainImageViews()[i],
                                                     msaaDepthView_, extent);
    }
}

void AntiAliasingDemo::destroyMsaaTargets() {
    auto &ctx = app_.ctx();
    for (auto fb : msaaFbs_) {
        vkDestroyFramebuffer(ctx.device(), fb, nullptr);
    }
    msaaFbs_.clear();
    if (msaaDepthView_) {
        vkDestroyImageView(ctx.device(), msaaDepthView_, nullptr);
        msaaDepthView_ = VK_NULL_HANDLE;
    }
    if (msaaDepth_) {
        vkDestroyImage(ctx.device(), msaaDepth_, nullptr);
        vkFreeMemory(ctx.device(), msaaDepthMem_, nullptr);
        msaaDepth_ = VK_NULL_HANDLE;
        msaaDepthMem_ = VK_NULL_HANDLE;
    }
    if (msaaColorView_) {
        vkDestroyImageView(ctx.device(), msaaColorView_, nullptr);
        msaaColorView_ = VK_NULL_HANDLE;
    }
    if (msaaColor_) {
        vkDestroyImage(ctx.device(), msaaColor_, nullptr);
        vkFreeMemory(ctx.device(), msaaColorMem_, nullptr);
        msaaColor_ = VK_NULL_HANDLE;
        msaaColorMem_ = VK_NULL_HANDLE;
    }
}

void AntiAliasingDemo::createTaaTargets() {
    auto &ctx = app_.ctx();
    const auto extent = ctx.swapChainExtent();
    for (int i = 0; i < 2; ++i) {
        VulkanUtil::createImage(ctx, extent.width, extent.height, 1, VK_SAMPLE_COUNT_1_BIT, ctx.swapChainImageFormat(),
                                VK_IMAGE_TILING_OPTIMAL,
                                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, historyTex_[i].image, historyTex_[i].memory);
        historyTex_[i].view = VulkanUtil::createImageView(ctx, historyTex_[i].image, ctx.swapChainImageFormat(),
                                                          VK_IMAGE_ASPECT_COLOR_BIT, 1);
    }
}

void AntiAliasingDemo::destroyTaaTargets() {
    auto &ctx = app_.ctx();
    for (int i = 0; i < 2; ++i) {
        destroyTexture(ctx, historyTex_[i]);
    }
}

void AntiAliasingDemo::createPipelines() {
    auto &ctx = app_.ctx();
    const auto extent = ctx.swapChainExtent();
    auto binding = vertexBinding();
    auto attrs = vertexAttributes();

    DemoRhi::PipelineInfo meshInfo{};
    meshInfo.vert = meshVert_;
    meshInfo.frag = meshFrag_;
    meshInfo.renderPass = app_.swapRenderPass();
    meshInfo.extent = extent;
    meshInfo.bindings = &binding;
    meshInfo.bindingCount = 1;
    meshInfo.attributes = attrs.data();
    meshInfo.attributeCount = static_cast<uint32_t>(attrs.size());
    meshInfo.layout = meshLayout_;
    meshInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    meshInfo.cullMode = VK_CULL_MODE_NONE;
    meshPipeline_ = DemoRhi::createGraphicsPipeline(ctx, meshInfo);

    meshInfo.renderPass = offscreenRp_;
    offscreenMeshPipeline_ = DemoRhi::createGraphicsPipeline(ctx, meshInfo);

    if (msaaSamples_ != VK_SAMPLE_COUNT_1_BIT) {
        meshInfo.renderPass = msaaRp_;
        meshInfo.samples = msaaSamples_;
        meshMsaaPipeline_ = DemoRhi::createGraphicsPipeline(ctx, meshInfo);
    }

    DemoRhi::PipelineInfo fxaaInfo{};
    fxaaInfo.vert = fxaaVert_;
    fxaaInfo.frag = fxaaFrag_;
    fxaaInfo.renderPass = app_.swapRenderPass();
    fxaaInfo.extent = extent;
    fxaaInfo.layout = fxaaLayout_;
    fxaaInfo.depthTest = false;
    fxaaInfo.cullMode = VK_CULL_MODE_NONE;
    fxaaPipeline_ = DemoRhi::createGraphicsPipeline(ctx, fxaaInfo);

    DemoRhi::PipelineInfo taaInfo{};
    taaInfo.vert = taaVert_;
    taaInfo.frag = taaFrag_;
    taaInfo.renderPass = app_.swapRenderPass();
    taaInfo.extent = extent;
    taaInfo.layout = taaLayout_;
    taaInfo.depthTest = false;
    taaInfo.cullMode = VK_CULL_MODE_NONE;
    taaPipeline_ = DemoRhi::createGraphicsPipeline(ctx, taaInfo);
}

void AntiAliasingDemo::destroyPipelines() {
    auto &ctx = app_.ctx();
    if (meshPipeline_) {
        vkDestroyPipeline(ctx.device(), meshPipeline_, nullptr);
        meshPipeline_ = VK_NULL_HANDLE;
    }
    if (offscreenMeshPipeline_) {
        vkDestroyPipeline(ctx.device(), offscreenMeshPipeline_, nullptr);
        offscreenMeshPipeline_ = VK_NULL_HANDLE;
    }
    if (meshMsaaPipeline_) {
        vkDestroyPipeline(ctx.device(), meshMsaaPipeline_, nullptr);
        meshMsaaPipeline_ = VK_NULL_HANDLE;
    }
    if (fxaaPipeline_) {
        vkDestroyPipeline(ctx.device(), fxaaPipeline_, nullptr);
        fxaaPipeline_ = VK_NULL_HANDLE;
    }
    if (taaPipeline_) {
        vkDestroyPipeline(ctx.device(), taaPipeline_, nullptr);
        taaPipeline_ = VK_NULL_HANDLE;
    }
}

// 统一场景绘制：地面 + 奶牛模型 + 金字塔/尖刺/拱门（AA 测试几何）
// msaaPass=true 时 RenderPass 有 3 个 clear（多采样颜色 + resolve + 深度）
// 场景几何 Pass：地面 + 奶牛模型 + 金字塔/尖刺/拱门（AA 测试用高对比边缘）
// msaaPass=true 时需 3 个 ClearValue（多采样颜色 + Resolve + 深度）
void AntiAliasingDemo::drawScene(VkCommandBuffer cmd, VkRenderPass rp, VkFramebuffer fb, VkExtent2D extent,
                                 VkPipeline pipeline, const glm::mat4 &view, const glm::mat4 &proj, bool msaaPass) {
    VkClearValue clears[3]{};
    clears[0].color = {{0.08f, 0.09f, 0.12f, 1.0f}};
    clears[1].color = {{0.08f, 0.09f, 0.12f, 1.0f}};
    clears[2].depthStencil = {1.0f, 0};

    VkClearValue swapClears[2]{};
    swapClears[0].color = {{0.08f, 0.09f, 0.12f, 1.0f}};
    swapClears[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo rpBegin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rpBegin.renderPass = rp;
    rpBegin.framebuffer = fb;
    rpBegin.renderArea.extent = extent;
    rpBegin.clearValueCount = msaaPass ? 3u : 2u;
    rpBegin.pClearValues = msaaPass ? clears : swapClears;
    vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{0, 0, static_cast<float>(extent.width), static_cast<float>(extent.height), 0, 1};
    VkRect2D scissor{{0, 0}, extent};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    glm::vec3 cameraPos = glm::vec3(glm::inverse(view)[3]);

    SceneUbo ubo{};
    ubo.view = view;
    ubo.proj = proj;
    ubo.cameraPos = glm::vec4(cameraPos, 1.0f);
    ubo.lightDir = glm::vec4(0.5f, 1.0f, 0.4f, 0.0f);

    if (uboMapped_) {
        std::memcpy(uboMapped_, &ubo, sizeof(SceneUbo));
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    auto drawMeshGpu = [&](const GpuMesh &mesh, VkDescriptorSet set, const glm::mat4 &model, const glm::vec4 &tint, int useTex) {
        if (mesh.indexCount == 0 || !mesh.vertexBuffer) return;
        PushConstants push{};
        push.model = model;
        push.colorTint = tint;
        push.useTexture = useTex;

        vkCmdPushConstants(cmd, meshLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &push);

        VkBuffer vb[] = {mesh.vertexBuffer};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cmd, 0, 1, vb, offsets);
        vkCmdBindIndexBuffer(cmd, mesh.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, meshLayout_, 0, 1, &set, 0, nullptr);
        vkCmdDrawIndexed(cmd, mesh.indexCount, 1, 0, 0, 0);
    };

    // 1. Ground Grid Floor
    drawMeshGpu(floorMesh_, gridSet_, glm::mat4(1.0f), glm::vec4(1.0f), 1);

    // 2. Multiple Cow Models (Spot)
    const float cowPositions[9][2] = {
        {-4.5f, -3.5f}, {0.0f, -3.5f}, {4.5f, -3.5f},
        {-4.5f,  0.0f}, {0.0f,  0.0f}, {4.5f,  0.0f},
        {-4.5f,  3.5f}, {0.0f,  3.5f}, {4.5f,  3.5f},
    };
    for (int i = 0; i < 9; ++i) {
        glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(cowPositions[i][0], 0.0f, cowPositions[i][1]));
        model = glm::rotate(model, glm::radians(25.0f * (i + 1)), glm::vec3(0, 1, 0));
        model = glm::scale(model, glm::vec3(0.85f));
        drawMeshGpu(spotMesh_, spotSet_, model, glm::vec4(1.0f), 1);
    }

    // 3. Sharp Oblique Pyramids
    drawMeshGpu(pyramidMesh_, colorSet_, glm::mat4(1.0f), glm::vec4(1.0f), 0);

    // 4. AA Spikes
    drawMeshGpu(spikesMesh_, colorSet_, glm::mat4(1.0f), glm::vec4(1.0f), 0);

    // 5. Thin Arches
    drawMeshGpu(archesMesh_, colorSet_, glm::mat4(1.0f), glm::vec4(1.0f), 0);

    vkCmdEndRenderPass(cmd);
}

void AntiAliasingDemo::render(DemoFrame &frame) {
    const auto extent = frame.extent;

    // ---- TAA：Halton 抖动 + 历史帧重投影混合 ----
    if (mode_ == Mode::TAA) {
        glm::vec2 jitter = getHaltonJitter(taaFrameCount_);
        currentJitter_ = glm::vec2(jitter.x / static_cast<float>(extent.width), jitter.y / static_cast<float>(extent.height));

        glm::mat4 jitteredProj = frame.proj;
        jitteredProj[2][0] += jitter.x * 2.0f / static_cast<float>(extent.width);
        jitteredProj[2][1] += jitter.y * 2.0f / static_cast<float>(extent.height);

        drawScene(frame.cmd, offscreenRp_, offscreenFb_, extent, offscreenMeshPipeline_, frame.view, jitteredProj,
                  false);

        uint32_t currIdx = taaFrameCount_ % 2;
        uint32_t prevIdx = (taaFrameCount_ + 1) % 2;

        writeTaaDescriptorSet(app_.ctx(), taaSet_[currIdx], offscreenColorView_, historyTex_[prevIdx].view,
                             offscreenDepthView_, sampler_);

        glm::mat4 currentUnjitteredViewProj = frame.proj * frame.view;
        glm::mat4 invViewProj = glm::inverse(currentUnjitteredViewProj);

        TaaPushConstants taaPush{};
        taaPush.invViewProj = invViewProj;
        taaPush.prevViewProj = (taaFrameCount_ == 0) ? currentUnjitteredViewProj : prevViewProj_;
        taaPush.jitterOffset = currentJitter_;
        taaPush.blendFactor = 0.10f;
        taaPush.firstFrame = (taaFrameCount_ == 0) ? 1 : 0;

        VkClearValue taaClears[2]{};
        taaClears[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
        taaClears[1].depthStencil = {1.0f, 0};
        VkRenderPassBeginInfo rpBegin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        rpBegin.renderPass = app_.swapRenderPass();
        rpBegin.framebuffer = frame.framebuffer;
        rpBegin.renderArea.extent = extent;
        rpBegin.clearValueCount = 2;
        rpBegin.pClearValues = taaClears;
        vkCmdBeginRenderPass(frame.cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport{0, 0, static_cast<float>(extent.width), static_cast<float>(extent.height), 0, 1};
        VkRect2D scissor{{0, 0}, extent};
        vkCmdSetViewport(frame.cmd, 0, 1, &viewport);
        vkCmdSetScissor(frame.cmd, 0, 1, &scissor);
        vkCmdBindPipeline(frame.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, taaPipeline_);
        vkCmdBindDescriptorSets(frame.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, taaLayout_, 0, 1, &taaSet_[currIdx], 0,
                                nullptr);
        vkCmdPushConstants(frame.cmd, taaLayout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(TaaPushConstants), &taaPush);
        vkCmdDraw(frame.cmd, 3, 1, 0, 0);
        vkCmdEndRenderPass(frame.cmd);

        prevViewProj_ = currentUnjitteredViewProj;
        taaFrameCount_++;
        return;
    }

    // ---- FXAA：离屏几何 + 全屏边缘模糊后处理 ----
    // ---- FXAA：离屏几何 + 全屏边缘检测模糊 ----
    if (mode_ == Mode::FXAA) {
        drawScene(frame.cmd, offscreenRp_, offscreenFb_, extent, offscreenMeshPipeline_, frame.view, frame.proj,
                  false);

        VkClearValue fxaaClears[2]{};
        fxaaClears[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
        fxaaClears[1].depthStencil = {1.0f, 0};
        VkRenderPassBeginInfo rpBegin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        rpBegin.renderPass = app_.swapRenderPass();
        rpBegin.framebuffer = frame.framebuffer;
        rpBegin.renderArea.extent = extent;
        rpBegin.clearValueCount = 2;
        rpBegin.pClearValues = fxaaClears;
        vkCmdBeginRenderPass(frame.cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport{0, 0, static_cast<float>(extent.width), static_cast<float>(extent.height), 0, 1};
        VkRect2D scissor{{0, 0}, extent};
        vkCmdSetViewport(frame.cmd, 0, 1, &viewport);
        vkCmdSetScissor(frame.cmd, 0, 1, &scissor);
        vkCmdBindPipeline(frame.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, fxaaPipeline_);
        vkCmdBindDescriptorSets(frame.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, fxaaLayout_, 0, 1, &fxaaSet_, 0, nullptr);
        vkCmdDraw(frame.cmd, 3, 1, 0, 0);
        vkCmdEndRenderPass(frame.cmd);
        return;
    }

    // ---- MSAA：多采样 RenderPass，硬件 Resolve 到 Swapchain 图像 ----
    if (mode_ == Mode::MSAA && msaaSamples_ != VK_SAMPLE_COUNT_1_BIT && !msaaFbs_.empty()) {
        drawScene(frame.cmd, msaaRp_, msaaFbs_[frame.imageIndex], extent, meshMsaaPipeline_, frame.view, frame.proj,
                  true);
        return;
    }

    // ---- None：无 AA，直接画到 Swapchain ----
    drawScene(frame.cmd, app_.swapRenderPass(), frame.framebuffer, extent, meshPipeline_, frame.view, frame.proj,
              false);
}
