#ifndef DEMO_TYPES_HPP
#define DEMO_TYPES_HPP

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

#include <cstddef>

struct MeshUbo {
    glm::mat4 model{1.0f};
    glm::mat4 view{1.0f};
    glm::mat4 proj{1.0f};
};

struct ColorVertex {
    glm::vec3 pos;
    glm::vec3 color;
};

struct OitVertex {
    glm::vec3 pos;
    glm::vec4 color;
};

inline VkVertexInputBindingDescription colorVertexBinding() {
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(ColorVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return binding;
}

inline VkVertexInputAttributeDescription colorVertexAttributes[2] = {
    {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(ColorVertex, pos)},
    {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(ColorVertex, color)},
};

inline VkVertexInputBindingDescription oitVertexBinding() {
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(OitVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return binding;
}

inline VkVertexInputAttributeDescription oitVertexAttributes[2] = {
    {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(OitVertex, pos)},
    {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(OitVertex, color)},
};

#endif
