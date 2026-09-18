#ifndef VULKAN_SHADER_HPP
#define VULKAN_SHADER_HPP

#include "VulkanContext.hpp"

namespace VulkanUtil {

VkShaderModule createShaderModule(const VulkanContext& ctx, const std::vector<char>& code);
VkShaderModule loadShaderModuleFromFile(const VulkanContext& ctx, const std::string& filename);

bool compileGlslToSpirv(const std::string& glslFile, const std::string& spirvFile);

// Load each SPIR-V file to verify build-time shader deployment (paths relative to exe CWD).
void verifyShaderModules(const VulkanContext& ctx, const std::vector<std::string>& spirvPaths);

} // namespace VulkanUtil

#endif // VULKAN_SHADER_HPP
