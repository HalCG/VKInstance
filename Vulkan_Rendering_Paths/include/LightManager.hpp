#ifndef LIGHT_MANAGER_HPP
#define LIGHT_MANAGER_HPP

// =============================================================================
// LightManager — 点光源数据与 Forward+ Tile 裁剪
// =============================================================================
//
// 【三种渲染路径如何使用本类】
//
//   Forward:    render() 开头 uploadToGpu()；片元循环 lightBuffer_ 全部 activeCount_ 盏
//   Deferred:   Pass2 uploadToGpu()；deferred_lighting.frag 同样循环全部灯
//   Forward+:   render() 开头 buildForwardPlusTiles() + uploadToGpu()
//               片元只读当前 Tile 的 tileCount/tileIndex
//
// 【为何 SSBO 而非 UBO 存灯数组】
//   UBO 通常 ≤ 64KB 且有对齐限制；512 盏灯 × 32 字节远超安全范围。
//   SSBO（storage buffer）容量大，片元着色器可随机索引 lights[i]。
//
// 【regenerate vs uploadToGpu】
//   regenerate() 只改 CPU 端 lights_，设 lightsDirty_
//   uploadToGpu() 在脏时 memcpy 到 lightMapped_（GPU 可见）
//
// =============================================================================

#include "AppConfig.hpp"
#include "RenderTypes.hpp"
#include "VulkanContext.hpp"

#include <vector>

class LightManager {
public:
    void init(VulkanContext &ctx);
    void shutdown();
    void regenerate(int activeCount);
    void uploadToGpu();

    int activeCount() const { return activeCount_; }
    VkBuffer lightBuffer() const { return lightBuffer_; }

    void buildForwardPlusTiles(int screenWidth, int screenHeight, const FrameCamera &camera);
    VkBuffer tileCountBuffer() const { return tileCountBuffer_; }
    VkBuffer tileIndexBuffer() const { return tileIndexBuffer_; }
    int tilesX() const { return tilesX_; }
    int tilesY() const { return tilesY_; }

private:
    VulkanContext *ctx_ = nullptr;
    std::vector<GpuPointLight> lights_;
    int activeCount_ = 64;
    bool lightsDirty_ = true;

    VkBuffer lightBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory lightMemory_ = VK_NULL_HANDLE;
    void *lightMapped_ = nullptr;

    VkBuffer tileCountBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory tileCountMemory_ = VK_NULL_HANDLE;
    void *tileCountMapped_ = nullptr;

    VkBuffer tileIndexBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory tileIndexMemory_ = VK_NULL_HANDLE;
    void *tileIndexMapped_ = nullptr;

    int tilesX_ = 0;
    int tilesY_ = 0;
    int maxTiles_ = 0;
    std::vector<uint32_t> countsCache_;
    std::vector<uint32_t> indicesCache_;
};

#endif
