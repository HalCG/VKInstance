#include "DemoMesh.hpp"

#include "VulkanBuffer.hpp"

#include <glm/gtc/constants.hpp>

#include <array>
#include <cmath>
#include <cstring>
#include <vector>

namespace DemoMesh {
namespace {

struct ColorVertex {
    glm::vec3 pos;
    glm::vec3 color;
};

struct OitVertex {
    glm::vec3 pos;
    glm::vec4 color;
};

Mesh uploadMesh(VulkanContext &ctx, const void *vertices, VkDeviceSize vertexSize, const std::vector<uint32_t> &indices) {
    Mesh mesh{};
    mesh.indexCount = static_cast<uint32_t>(indices.size());

    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory stagingMem = VK_NULL_HANDLE;
    VulkanUtil::createBuffer(ctx, vertexSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging,
                             stagingMem);
    void *mapped = nullptr;
    vkMapMemory(ctx.device(), stagingMem, 0, vertexSize, 0, &mapped);
    std::memcpy(mapped, vertices, vertexSize);
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

std::vector<ColorVertex> buildCube(const glm::vec3 &center, float half, const glm::vec3 &color) {
    const float h = half;
    const glm::vec3 c = center;
    return {
        {c + glm::vec3(-h, -h, h), color}, {c + glm::vec3(h, -h, h), color}, {c + glm::vec3(h, h, h), color},
        {c + glm::vec3(-h, h, h), color}, {c + glm::vec3(-h, -h, -h), color}, {c + glm::vec3(h, -h, -h), color},
        {c + glm::vec3(h, h, -h), color}, {c + glm::vec3(-h, h, -h), color},
    };
}

const std::vector<uint32_t> kCubeIndices = {
    0, 1, 2, 2, 3, 0, 1, 5, 6, 6, 2, 1, 5, 4, 7, 7, 6, 5,
    4, 0, 3, 3, 7, 4, 3, 2, 6, 6, 7, 3, 4, 5, 1, 1, 0, 4,
};

} // namespace

void destroy(VulkanContext &ctx, Mesh &mesh) {
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

Mesh createAASpikes(VulkanContext &ctx) {
    std::vector<ColorVertex> vertices;
    std::vector<uint32_t> indices;
    uint32_t base = 0;

    const std::array<glm::vec3, 6> colors = {
        glm::vec3(1, 0.2f, 0.2f), glm::vec3(0.2f, 1, 0.3f), glm::vec3(0.2f, 0.5f, 1),
        glm::vec3(1, 0.9f, 0.1f), glm::vec3(1, 0.4f, 0.9f), glm::vec3(0.3f, 1, 1),
    };

    for (int x = -2; x <= 2; ++x) {
        for (int z = -2; z <= 2; ++z) {
            const glm::vec3 color = colors[(x + z + 4) % colors.size()];
            const glm::vec3 center(static_cast<float>(x) * 1.4f, 0.6f + 0.15f * static_cast<float>((x * z) % 3),
                                   static_cast<float>(z) * 1.4f);
            const auto cube = buildCube(center, 0.35f, color);
            vertices.insert(vertices.end(), cube.begin(), cube.end());
            for (uint32_t idx : kCubeIndices) {
                indices.push_back(base + idx);
            }
            base += 8;
        }
    }

    return uploadMesh(ctx, vertices.data(), sizeof(ColorVertex) * vertices.size(), indices);
}

Mesh createOITQuads(VulkanContext &ctx) {
    std::vector<OitVertex> vertices;
    std::vector<uint32_t> indices;
    uint32_t base = 0;

    struct QuadDef {
        glm::vec3 center;
        glm::vec4 color;
        float size;
    };

    // Match OpenGL OIT Depth Peeling scene translucent quads (Red, Green, Blue windows)
    const std::array<QuadDef, 3> quads = {{
        {glm::vec3(-0.5f, 0.0f, 0.8f),  glm::vec4(1.0f, 0.25f, 0.25f, 0.60f), 1.1f}, // Red Window Quad
        {glm::vec3(0.2f, -0.5f, -1.0f), glm::vec4(0.25f, 0.95f, 0.35f, 0.60f), 1.1f}, // Green Window Quad
        {glm::vec3(0.2f, 0.0f, -0.5f),  glm::vec4(0.25f, 0.55f, 1.00f, 0.60f), 1.1f}, // Blue Window Quad
    }};

    for (const auto &q : quads) {
        const float s = q.size;
        const std::array<OitVertex, 4> quadVertices = {
            OitVertex{q.center + glm::vec3(-s, -s, 0.0f), q.color},
            OitVertex{q.center + glm::vec3(s, -s, 0.0f), q.color},
            OitVertex{q.center + glm::vec3(s, s, 0.0f), q.color},
            OitVertex{q.center + glm::vec3(-s, s, 0.0f), q.color},
        };
        vertices.insert(vertices.end(), quadVertices.begin(), quadVertices.end());
        indices.insert(indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
        base += 4;
    }

    return uploadMesh(ctx, vertices.data(), sizeof(OitVertex) * vertices.size(), indices);
}

Mesh createFloor(VulkanContext &ctx) {
    const std::vector<OitVertex> vertices = {
        {{-6, -1.5f, -6}, {0.18f, 0.20f, 0.24f, 1.0f}},
        {{6, -1.5f, -6}, {0.18f, 0.20f, 0.24f, 1.0f}},
        {{6, -1.5f, 6}, {0.18f, 0.20f, 0.24f, 1.0f}},
        {{-6, -1.5f, 6}, {0.18f, 0.20f, 0.24f, 1.0f}},
    };
    const std::vector<uint32_t> indices = {0, 1, 2, 0, 2, 3};
    return uploadMesh(ctx, vertices.data(), sizeof(OitVertex) * vertices.size(), indices);
}

} // namespace DemoMesh
