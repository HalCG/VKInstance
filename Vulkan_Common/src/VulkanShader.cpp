#include "VulkanShader.hpp"
#include <cstdlib>

namespace VulkanUtil {

VkShaderModule createShaderModule(const VulkanContext& ctx, const std::vector<char>& code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(ctx.device(), &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shader module!");
    }

    return shaderModule;
}

VkShaderModule loadShaderModuleFromFile(const VulkanContext& ctx, const std::string& filename) {
    auto code = readFile(filename);
    return createShaderModule(ctx, code);
}

void verifyShaderModules(const VulkanContext& ctx, const std::vector<std::string>& spirvPaths) {
    for (const auto& path : spirvPaths) {
        VkShaderModule module = loadShaderModuleFromFile(ctx, path);
        vkDestroyShaderModule(ctx.device(), module, nullptr);
    }
}

bool compileGlslToSpirv(const std::string& glslFile, const std::string& spirvFile) {
#ifdef VK_WS_GLSLC_EXECUTABLE
    std::string glslc = VK_WS_GLSLC_EXECUTABLE;
#else
    const char* vulkanSdk = std::getenv("VULKAN_SDK");
    if (!vulkanSdk) {
        return false;
    }
    std::string glslc = std::string(vulkanSdk) + "/Bin/glslc.exe";
#endif

    std::string cmd = "\"" + glslc + "\" \"" + glslFile + "\" -o \"" + spirvFile + "\"";
    int ret = std::system(cmd.c_str());
    return ret == 0;
}

} // namespace VulkanUtil
