// =============================================================================
// LightManager — 点光源 SSBO + Forward+ 屏幕 Tile 裁剪
// =============================================================================
//
// 【GpuPointLight 布局（std430，与 Shader 中 PointLight 一致）】
//   vec4 positionRadius   — xyz=世界坐标, w=影响半径
//   vec4 colorIntensity   — rgb=颜色, w=强度倍率
//
// 【三条 GPU 缓冲】
//   lightBuffer_      全部光源（最多 kMaxLights=512），Forward/Deferred/Forward+ 共用
//   tileCountBuffer_  每 Tile 收录灯数（仅 Forward+）
//   tileIndexBuffer_  每 Tile 灯索引列表（仅 Forward+）
//
// 均使用 HOST_VISIBLE | HOST_COHERENT：每帧 memcpy 即可，无需显式 flush
// （小数据量 Demo 可接受；量产应换 DEVICE_LOCAL + 专用上传队列）
//
// 【regenerate】
//   固定种子 rng(1337)，按 [ / ] 预设切换 activeCount_，lightsDirty_=true
//   uploadToGpu() 在 Renderer::render 开头把脏数据 memcpy 到 lightBuffer_
//
// 【buildForwardPlusTiles 算法概要】
//   对每盏灯：
//     1. 用半径构造 8 个包围盒角点（世界空间）
//     2. viewProj 投影到屏幕，得 [minX,minY]-[maxX,maxY] 像素矩形
//     3. 若穿过近裁剪面（w<=0），保守地覆盖全屏
//     4. 矩形 ÷ tileSize → Tile 范围，把 light 下标写入 indicesCache_
//     5. 每 Tile 最多 kMaxLightsPerTile 盏，超出则丢弃（Demo 简化）
//
// =============================================================================

#include "LightManager.hpp"
#include "VulkanBuffer.hpp"
#include <algorithm>
#include <cstring>
#include <glm/gtc/matrix_transform.hpp>
#include <random>

void LightManager::init(VulkanContext &ctx) {
    ctx_ = &ctx;
    lights_.resize(AppConfig::kMaxLights);
    regenerate(activeCount_);

    const VkDeviceSize lightSize = lights_.size() * sizeof(GpuPointLight);
    VulkanUtil::createBuffer(ctx, lightSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, lightBuffer_,
                             lightMemory_);
    vkMapMemory(ctx.device(), lightMemory_, 0, lightSize, 0, &lightMapped_);
    uploadToGpu();

    // 预分配 Tile 缓冲：按 4K 分辨率上限 (3840×2160, tile=16 → 240×135=32400 tiles)
    maxTiles_ = (3840 / AppConfig::kTileSize) * (2160 / AppConfig::kTileSize);
    const VkDeviceSize countSize = static_cast<VkDeviceSize>(maxTiles_) * sizeof(uint32_t);
    const VkDeviceSize indexSize =
        static_cast<VkDeviceSize>(maxTiles_) * AppConfig::kMaxLightsPerTile * sizeof(uint32_t);

    VulkanUtil::createBuffer(ctx, countSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                             tileCountBuffer_, tileCountMemory_);
    vkMapMemory(ctx.device(), tileCountMemory_, 0, countSize, 0, &tileCountMapped_);

    VulkanUtil::createBuffer(ctx, indexSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                             tileIndexBuffer_, tileIndexMemory_);
    vkMapMemory(ctx.device(), tileIndexMemory_, 0, indexSize, 0, &tileIndexMapped_);
}

void LightManager::shutdown() {
    if (!ctx_) {
        return;
    }
    if (lightMapped_) {
        vkUnmapMemory(ctx_->device(), lightMemory_);
        lightMapped_ = nullptr;
    }
    if (tileCountMapped_) {
        vkUnmapMemory(ctx_->device(), tileCountMemory_);
        tileCountMapped_ = nullptr;
    }
    if (tileIndexMapped_) {
        vkUnmapMemory(ctx_->device(), tileIndexMemory_);
        tileIndexMapped_ = nullptr;
    }
    if (lightBuffer_) {
        vkDestroyBuffer(ctx_->device(), lightBuffer_, nullptr);
        vkFreeMemory(ctx_->device(), lightMemory_, nullptr);
        lightBuffer_ = VK_NULL_HANDLE;
    }
    if (tileCountBuffer_) {
        vkDestroyBuffer(ctx_->device(), tileCountBuffer_, nullptr);
        vkFreeMemory(ctx_->device(), tileCountMemory_, nullptr);
        tileCountBuffer_ = VK_NULL_HANDLE;
    }
    if (tileIndexBuffer_) {
        vkDestroyBuffer(ctx_->device(), tileIndexBuffer_, nullptr);
        vkFreeMemory(ctx_->device(), tileIndexMemory_, nullptr);
        tileIndexBuffer_ = VK_NULL_HANDLE;
    }
    ctx_ = nullptr;
}

void LightManager::regenerate(int activeCount) {
    activeCount_ = std::max(1, std::min(activeCount, AppConfig::kMaxLights));
    lightsDirty_ = true;

    std::mt19937 rng(1337);
    std::uniform_real_distribution<float> posX(-4.0f, 4.0f);
    std::uniform_real_distribution<float> posY(0.5f, 3.5f);
    std::uniform_real_distribution<float> posZ(-4.0f, 4.0f);
    std::uniform_real_distribution<float> hue(0.0f, 1.0f);
    std::uniform_real_distribution<float> intensity(0.6f, 1.4f);

    for (int i = 0; i < AppConfig::kMaxLights; ++i) {
        GpuPointLight light{};
        if (i < activeCount_) {
            light.positionRadius = glm::vec4(posX(rng), posY(rng), posZ(rng), 3.5f);
            const float h = hue(rng);
            const glm::vec3 color =
                glm::abs(glm::vec3(h * 6.0f, h * 6.0f + 2.0f, h * 6.0f + 4.0f) - glm::vec3(3.0f));
            light.colorIntensity = glm::vec4(color, intensity(rng));
        }
        lights_[static_cast<size_t>(i)] = light;
    }
}

void LightManager::uploadToGpu() {
    if (!lightsDirty_ || !lightMapped_) {
        return;
    }
    std::memcpy(lightMapped_, lights_.data(), lights_.size() * sizeof(GpuPointLight));
    lightsDirty_ = false;
}

// 将每盏灯的 3D 包围盒投影到屏幕，求其覆盖的 Tile 范围，写入 SSBO
void LightManager::buildForwardPlusTiles(int screenWidth, int screenHeight, const FrameCamera &camera) {
    tilesX_ = (screenWidth + AppConfig::kTileSize - 1) / AppConfig::kTileSize;
    tilesY_ = (screenHeight + AppConfig::kTileSize - 1) / AppConfig::kTileSize;
    const int tileCount = std::min(tilesX_ * tilesY_, maxTiles_);

    countsCache_.assign(static_cast<size_t>(tileCount), 0);
    indicesCache_.assign(static_cast<size_t>(tileCount) * AppConfig::kMaxLightsPerTile, 0xFFFFFFFFu);

    const glm::mat4 viewProj = camera.projection * camera.view;

    for (int i = 0; i < activeCount_; ++i) {
        const glm::vec3 center = glm::vec3(lights_[static_cast<size_t>(i)].positionRadius);
        const float radius = lights_[static_cast<size_t>(i)].positionRadius.w;

        // 光源影响球体的 AABB 八个角点
        glm::vec3 corners[8];
        int idx = 0;
        for (int x = 0; x < 2; ++x) {
            for (int y = 0; y < 2; ++y) {
                for (int z = 0; z < 2; ++z) {
                    corners[idx++] =
                        center + glm::vec3((x ? 1.0f : -1.0f) * radius, (y ? 1.0f : -1.0f) * radius,
                                           (z ? 1.0f : -1.0f) * radius);
                }
            }
        }

        float minX = static_cast<float>(screenWidth);
        float minY = static_cast<float>(screenHeight);
        float maxX = 0.0f;
        float maxY = 0.0f;
        bool anyInFront = false;
        bool intersectsNearPlane = false;

        for (const glm::vec3 &world : corners) {
            glm::vec4 clip = viewProj * glm::vec4(world, 1.0f);
            if (clip.w <= 0.0f) {
                intersectsNearPlane = true;
                continue;
            }
            anyInFront = true;
            const glm::vec3 ndc = glm::vec3(clip) / clip.w;
            const float sx = (ndc.x * 0.5f + 0.5f) * screenWidth;
            const float sy = (ndc.y * 0.5f + 0.5f) * screenHeight;
            minX = std::min(minX, sx);
            minY = std::min(minY, sy);
            maxX = std::max(maxX, sx);
            maxY = std::max(maxY, sy);
        }

        if (!anyInFront && !intersectsNearPlane) {
            continue; // 完全在相机后方，跳过
        }

        if (intersectsNearPlane) {
            // 保守策略：穿过近裁剪面时视为影响全屏（避免漏光）
            minX = 0.0f;
            minY = 0.0f;
            maxX = static_cast<float>(screenWidth - 1);
            maxY = static_cast<float>(screenHeight - 1);
        } else {
            minX = glm::clamp(minX, 0.0f, static_cast<float>(screenWidth - 1));
            maxX = glm::clamp(maxX, 0.0f, static_cast<float>(screenWidth - 1));
            minY = glm::clamp(minY, 0.0f, static_cast<float>(screenHeight - 1));
            maxY = glm::clamp(maxY, 0.0f, static_cast<float>(screenHeight - 1));
        }

        const int tileMinX = static_cast<int>(minX) / AppConfig::kTileSize;
        const int tileMaxX = static_cast<int>(maxX) / AppConfig::kTileSize;
        const int tileMinY = static_cast<int>(minY) / AppConfig::kTileSize;
        const int tileMaxY = static_cast<int>(maxY) / AppConfig::kTileSize;

        for (int ty = tileMinY; ty <= tileMaxY; ++ty) {
            for (int tx = tileMinX; tx <= tileMaxX; ++tx) {
                if (tx < 0 || ty < 0 || tx >= tilesX_ || ty >= tilesY_) {
                    continue;
                }
                const int tileIndex = ty * tilesX_ + tx;
                if (tileIndex >= tileCount) {
                    continue;
                }
                uint32_t &count = countsCache_[static_cast<size_t>(tileIndex)];
                if (count >= static_cast<uint32_t>(AppConfig::kMaxLightsPerTile)) {
                    continue; // 单 Tile 灯数上限，超出丢弃
                }
                indicesCache_[static_cast<size_t>(tileIndex) * AppConfig::kMaxLightsPerTile + count] =
                    static_cast<uint32_t>(i);
                ++count;
            }
        }
    }

    if (tileCountMapped_) {
        std::memcpy(tileCountMapped_, countsCache_.data(), countsCache_.size() * sizeof(uint32_t));
    }
    if (tileIndexMapped_) {
        std::memcpy(tileIndexMapped_, indicesCache_.data(), indicesCache_.size() * sizeof(uint32_t));
    }
}
