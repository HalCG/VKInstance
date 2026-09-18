#ifndef DEMO_MESH_HPP
#define DEMO_MESH_HPP

#include "VulkanContext.hpp"

namespace DemoMesh {

struct Mesh {
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory indexMemory = VK_NULL_HANDLE;
    uint32_t indexCount = 0;
};

void destroy(VulkanContext &ctx, Mesh &mesh);
Mesh createAASpikes(VulkanContext &ctx);
Mesh createOITQuads(VulkanContext &ctx);
Mesh createFloor(VulkanContext &ctx);

} // namespace DemoMesh

#endif
