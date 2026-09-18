#include "scene.h"
#include "utils.h"
#include "quaternion.h"
#include <cmath>
#include <stdexcept>

static VulkanContext* sCtx = nullptr;
static uint32_t sCanvasWidth = 1280;
static uint32_t sCanvasHeight = 720;

// 交互相机与矩阵
static TrackballCamera sCamera;
static matrix4 sProjectionMatrix;
static matrix4 sViewMatrix;
static matrix4 sModelMatrix;

// UBO 缓冲
static GlobalConstants sGlobalConstantsData;
static VulkanBuffer sGlobalConstantsBuffer;

// Nanite 核心 SSBO 显存缓冲
static VulkanBuffer sNaniteMesh;                        // Nanite 原始顶点与簇元数据 SSBO
static VulkanBuffer sBVH;                               // BVH 层次树节点 SSBO
static VulkanBuffer sWorkArgs[2];                       // GPU 间接绘制命令 (VkDrawIndirectCommand) 与 Ping-Pong 调度缓冲
static VulkanBuffer sMainAndPostNodeAndClusterBatches;  // 节点队列与可见簇收集缓冲
static VulkanBuffer sVisiableClusterSWHW;               // 筛选出的可见 Cluster 列表 SSBO
static VulkanBuffer sVisBuffer64;                       // 64 位 Visibility Buffer (存放深度 + ClusterID)

// 2D 存储图像 (Visualization Storage Image)
static VulkanImage sVisualizationTexture;

// Compute 描述符与计算管线
static VulkanComputePipeline sRasterClearPipeline;      // Pass 1: 状态重置与 VisBuffer64 清空
static VulkanComputePipeline sNodeAndClusterCullPipeline[4]; // Pass 2: BVH 树 4 轮 Ping-Pong 节点剔除
static VulkanComputePipeline sClusterCullPipeline;      // Pass 3: Cluster 细粒度剔除与 VkDrawIndirectCommand 生成
static VulkanComputePipeline sVisualizationPipeline;    // Pass 5: VisBuffer64 解码与伪彩纹理生成

// Graphics 图形管线
static VulkanGraphicsPipeline sHWRasterizePipeline;    // Pass 4: 硬件光栅化 (vkCmdDrawIndirect 零 CPU 拷贝间接绘制)
static VulkanGraphicsPipeline sFSQPipeline;            // Pass 6: 全屏 Blit 呈现至 Swapchain

static const VkDeviceSize _4MB = 4 * 1024 * 1024;       // 4MB 标准 SSBO 分配尺寸

// ==============================================================================
// 场景初始化：分配 GPU SSBO、加载二进制 Nanite 资产、创建所有 Vulkan 管线
// ==============================================================================
void InitScene(VulkanContext* ctx, uint32_t width, uint32_t height) {
    sCtx = ctx;
    sCanvasWidth = width;
    sCanvasHeight = height;

    // 与 OpenGL 版 0015 对齐：相机初始位姿 + 模型绕 X 轴 180°
    sCamera.Init(float4(-330.0f, 330.0f, -330.0f), float4(0.0f, 80.0f, 0.0f));
    sProjectionMatrix.Perspective(90.0f, float(sCanvasWidth) / float(sCanvasHeight), 1.0f, 10000.0f);
    sModelMatrix.LoadIdentity();
    {
        matrix3 scaleMatrix;
        scaleMatrix.LoadIdentity();
        matrix3 lt3x3 = scaleMatrix * quaternion(180.0f, 0.0f, 0.0f).toMatrix3();
        sModelMatrix.SetLeftTop3x3(lt3x3);
    }
    sGlobalConstantsData.Misc0[0] = sCanvasWidth;
    sGlobalConstantsData.Misc0[1] = sCanvasHeight;

    // 分配 GlobalConstants UBO 缓冲
    sGlobalConstantsBuffer.Create(sCtx, sizeof(GlobalConstants), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    // 1. 从磁盘加载 Mitsuba BVH 二进制数据并上传至 GPU 显存
    {
        size_t fileSize = 0;
        unsigned char* fileContent = LoadFileContent("Res/mitsuba.bvh", fileSize);
        if (!fileContent || fileSize == 0) {
            throw std::runtime_error("缺少 Res/mitsuba.bvh，请从 0015 复制或运行 scripts/CopyRuntimeDeps.cmd");
        }
        sBVH.Create(sCtx, fileSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        sBVH.Upload(fileContent, fileSize);
        delete[] fileContent;
    }

    // 2. 从磁盘加载 Nanite Mesh 原始顶点与 Cluster 数据并上传至 GPU 显存
    {
        size_t fileSize = 0;
        unsigned char* fileContent = LoadFileContent("Res/mitsuba.nanitemesh", fileSize);
        if (!fileContent || fileSize == 0) {
            throw std::runtime_error("缺少 Res/mitsuba.nanitemesh，请从 0015 复制或运行 scripts/CopyRuntimeDeps.cmd");
        }
        sNaniteMesh.Create(sCtx, fileSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        sNaniteMesh.Upload(fileContent, fileSize);
        delete[] fileContent;
    }

    // 3. 分配 WorkArgs (含有 VkDrawIndirectCommand) 与 Batch 缓冲区
    sWorkArgs[0].Create(sCtx, _4MB, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    sWorkArgs[1].Create(sCtx, _4MB, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    sMainAndPostNodeAndClusterBatches.Create(sCtx, _4MB, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    sVisiableClusterSWHW.Create(sCtx, _4MB, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    // 4. 分配 64 位 VisBuffer64 (全屏像素 depth + clusterID 打包)
    VkDeviceSize visBufferSize = sCanvasWidth * sCanvasHeight * sizeof(uint64_t);
    sVisBuffer64.Create(sCtx, visBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    // 5. 创建全屏 RGBA32F 存储图像 Storage Image
    sVisualizationTexture.CreateStorageImage(sCtx, sCanvasWidth, sCanvasHeight, VK_FORMAT_R32G32B32A32_SFLOAT);

    // ==============================================================================
    // 构建各个 Compute 与 Graphics 管线
    // ==============================================================================

    // 【Pass 1: RasterClear 管线】
    {
        std::vector<VkDescriptorSetLayoutBinding> bindings = {
            { 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr }
        };
        sRasterClearPipeline.Create(sCtx, "Res/Shaders/RasterClear.spv", bindings);
        sRasterClearPipeline.BindSSBO(0, sWorkArgs[0].mBuffer);
        sRasterClearPipeline.BindSSBO(1, sWorkArgs[1].mBuffer);
        sRasterClearPipeline.BindSSBO(2, sVisBuffer64.mBuffer);
        sRasterClearPipeline.BindUniformBuffer(3, sGlobalConstantsBuffer.mBuffer, sizeof(GlobalConstants));
    }

    // 【Pass 2: NodeAndClusterCull (Ping-Pong 4轮遍历) 管线】
    {
        std::vector<VkDescriptorSetLayoutBinding> bindings = {
            { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr }
        };

        for (int i = 0; i < 4; i++) {
            int currentWorkArgsIdx = i % 2;
            int nextWorkArgsIdx = (i + 1) % 2;

            sNodeAndClusterCullPipeline[i].Create(sCtx, "Res/Shaders/NodeAndClusterCull.spv", bindings);
            sNodeAndClusterCullPipeline[i].BindUniformBuffer(0, sGlobalConstantsBuffer.mBuffer, sizeof(GlobalConstants));
            sNodeAndClusterCullPipeline[i].BindSSBO(1, sBVH.mBuffer);
            sNodeAndClusterCullPipeline[i].BindSSBO(2, sWorkArgs[currentWorkArgsIdx].mBuffer);
            sNodeAndClusterCullPipeline[i].BindSSBO(3, sWorkArgs[nextWorkArgsIdx].mBuffer);
            sNodeAndClusterCullPipeline[i].BindSSBO(4, sMainAndPostNodeAndClusterBatches.mBuffer);
        }
    }

    // 【Pass 3: ClusterCull 管线】
    {
        std::vector<VkDescriptorSetLayoutBinding> bindings = {
            { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr }
        };
        sClusterCullPipeline.Create(sCtx, "Res/Shaders/ClusterCull.spv", bindings);
        sClusterCullPipeline.BindUniformBuffer(0, sGlobalConstantsBuffer.mBuffer, sizeof(GlobalConstants));
        sClusterCullPipeline.BindSSBO(1, sNaniteMesh.mBuffer);
        sClusterCullPipeline.BindSSBO(2, sWorkArgs[0].mBuffer);
        sClusterCullPipeline.BindSSBO(3, sVisiableClusterSWHW.mBuffer);
        sClusterCullPipeline.BindSSBO(4, sMainAndPostNodeAndClusterBatches.mBuffer);
    }

    // 【Pass 5: Visualization 管线】
    {
        std::vector<VkDescriptorSetLayoutBinding> bindings = {
            { 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr }
        };
        sVisualizationPipeline.Create(sCtx, "Res/Shaders/Visualization.spv", bindings);
        sVisualizationPipeline.BindSSBO(0, sVisBuffer64.mBuffer);
        sVisualizationPipeline.BindStorageImage(1, sVisualizationTexture.mImageView);
        sVisualizationPipeline.BindUniformBuffer(2, sGlobalConstantsBuffer.mBuffer, sizeof(GlobalConstants));
    }

    // 【Pass 4: HWRasterize 硬件光栅化图形管线】
    {
        std::vector<VkDescriptorSetLayoutBinding> bindings = {
            { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
            { 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr },
            { 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr },
            { 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr }
        };
        sHWRasterizePipeline.CreateHWRasterize(sCtx, "Res/Shaders/HWRasterizeVS.spv", "Res/Shaders/HWRasterizeFS.spv", bindings);
        sHWRasterizePipeline.BindUniformBuffer(0, sGlobalConstantsBuffer.mBuffer, sizeof(GlobalConstants));
        sHWRasterizePipeline.BindSSBO(1, sNaniteMesh.mBuffer);
        sHWRasterizePipeline.BindSSBO(2, sVisiableClusterSWHW.mBuffer);
        sHWRasterizePipeline.BindSSBO(3, sVisBuffer64.mBuffer);
    }

    // 【Pass 6: FSQ 全屏 Present 呈现图形管线】
    {
        std::vector<VkDescriptorSetLayoutBinding> bindings = {
            { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr }
        };
        sFSQPipeline.CreateFSQ(sCtx, "Res/Shaders/FSQVS.spv", "Res/Shaders/FSQFS.spv", bindings);
        sFSQPipeline.BindCombinedImageSampler(0, sVisualizationTexture.mImageView, sVisualizationTexture.mSampler);
    }
}

// ==============================================================================
// 核心单帧渲染流程：更新 Uniforms、发送 Command Memory Barrier 并录制执行 GPU 渲染命令
// ==============================================================================
void RenderOneFrame(VulkanContext* ctx, float frameTime) {
    // 1. 提取最新相机 View/Projection 矩阵并更新上传至 GPU UBO
    sViewMatrix = sCamera.GetViewMatrix();
    memcpy(sGlobalConstantsData.ProjectionMatrix, sProjectionMatrix.v, sizeof(sProjectionMatrix.v));
    memcpy(sGlobalConstantsData.ViewMatrix, sViewMatrix.v, sizeof(sViewMatrix.v));
    memcpy(sGlobalConstantsData.ModelMatrix, sModelMatrix.v, sizeof(sModelMatrix.v));

    const float ViewToPixels = 0.5f * sProjectionMatrix.v[5] * float(sCanvasHeight);
    const float LODScale = ViewToPixels / 1.0f;
    const float LODScaleHW = ViewToPixels / 32.0f;

    float4 eye = sCamera.GetEye();
    float4 fwd = sCamera.GetForward();
    sGlobalConstantsData.CameraPositionWS[0] = eye.x;
    sGlobalConstantsData.CameraPositionWS[1] = eye.y;
    sGlobalConstantsData.CameraPositionWS[2] = eye.z;
    sGlobalConstantsData.CameraPositionWS[3] = LODScale;

    sGlobalConstantsData.ViewDirectionWS[0] = fwd.x;
    sGlobalConstantsData.ViewDirectionWS[1] = fwd.y;
    sGlobalConstantsData.ViewDirectionWS[2] = fwd.z;
    sGlobalConstantsData.ViewDirectionWS[3] = LODScaleHW;
    sGlobalConstantsData.Misc0[0] = sCanvasWidth;
    sGlobalConstantsData.Misc0[1] = sCanvasHeight;

    sGlobalConstantsBuffer.Upload(&sGlobalConstantsData, sizeof(GlobalConstants));

    // 2. 开启 Vulkan 当前帧录制
    ctx->BeginFrame();
    VkCommandBuffer cmd = ctx->mCommandBuffer;

    // 全局内存屏障 (保证 Compute Shader 写入 SSBO 后后续 Pass 完全可见)
    auto GlobalMemoryBarrier = [cmd]() {
        VkMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
    };

    // --------------------------------------------------------------------------
    // 【管线阶段 1】：RasterClear 计算 Pass
    // --------------------------------------------------------------------------
    sRasterClearPipeline.Dispatch(cmd, (uint32_t)ceilf((float)sCanvasWidth / 8.0f), (uint32_t)ceilf((float)sCanvasHeight / 8.0f), 1);
    GlobalMemoryBarrier();

    // --------------------------------------------------------------------------
    // 【管线阶段 2】：BVH 树节点 Ping-Pong 剔除 (4 轮迭代)
    // --------------------------------------------------------------------------
    for (int i = 0; i < 4; i++) {
        sNodeAndClusterCullPipeline[i].Dispatch(cmd, 1, 1, 1);
        GlobalMemoryBarrier();
    }

    // --------------------------------------------------------------------------
    // 【管线阶段 3】：Cluster 级细粒度剔除 (生成 VkDrawIndirectCommand)
    // --------------------------------------------------------------------------
    sClusterCullPipeline.Dispatch(cmd, 1, 1, 1);
    GlobalMemoryBarrier();

    // --------------------------------------------------------------------------
    // 【管线阶段 4】：硬件间接绘制（离屏 FB + VisBuffer atomicMin，不写 Swapchain）
    // --------------------------------------------------------------------------
    {
        sHWRasterizePipeline.BeginRenderPass(cmd, sHWRasterizePipeline.mFramebuffer, ctx->mSwapchainExtent, false, nullptr);
        sHWRasterizePipeline.DrawIndirect(cmd, sWorkArgs[0].mBuffer, 0);
        sHWRasterizePipeline.EndRenderPass(cmd);
    }
    GlobalMemoryBarrier();

    // --------------------------------------------------------------------------
    // 【管线阶段 5】：Visualization 计算 Pass (将 VisBuffer64 解码输出为伪彩图像)
    // --------------------------------------------------------------------------
    sVisualizationPipeline.Dispatch(cmd, (uint32_t)ceilf((float)sCanvasWidth / 8.0f), (uint32_t)ceilf((float)sCanvasHeight / 8.0f), 1);

    {
        VkImageMemoryBarrier visBarrier{};
        visBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        visBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        visBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        visBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        visBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        visBarrier.image = sVisualizationTexture.mImage;
        visBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        visBarrier.subresourceRange.levelCount = 1;
        visBarrier.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &visBarrier);
    }

    // --------------------------------------------------------------------------
    // 【管线阶段 6】：FSQ → Swapchain
    // --------------------------------------------------------------------------
    {
        const float clearColor[4] = { 0.1f, 0.4f, 0.6f, 1.0f };
        sFSQPipeline.BeginRenderPass(cmd, ctx->mSwapchainFramebuffers[ctx->mCurrentImageIndex], ctx->mSwapchainExtent, true, clearColor);
        sFSQPipeline.Draw(cmd, 3, 1);
        sFSQPipeline.EndRenderPass(cmd);
    }

    // 提交帧渲染并 Present 到窗口表面
    ctx->EndFrame();
}

// ==============================================================================
// 释放场景管线与显存 Buffer 资源
// ==============================================================================
void CleanupScene() {
    sRasterClearPipeline.Destroy();
    for (int i = 0; i < 4; i++) sNodeAndClusterCullPipeline[i].Destroy();
    sClusterCullPipeline.Destroy();
    sVisualizationPipeline.Destroy();

    sHWRasterizePipeline.Destroy();
    sFSQPipeline.Destroy();

    sVisualizationTexture.Destroy();

    sVisBuffer64.Destroy();
    sVisiableClusterSWHW.Destroy();
    sMainAndPostNodeAndClusterBatches.Destroy();
    sWorkArgs[1].Destroy();
    sWorkArgs[0].Destroy();

    sNaniteMesh.Destroy();
    sBVH.Destroy();

    sGlobalConstantsBuffer.Destroy();
}

// 响应键盘/鼠标交互
void OnKeyUp(WPARAM wParam) {
    if (wParam == VK_UP) {
        sCamera.mRadius = MaxValue(1.0f, sCamera.mRadius * 0.8f);
    } else if (wParam == VK_DOWN) {
        sCamera.mRadius = MinValue(5000.0f, sCamera.mRadius * 1.2f);
    }
}

void OnMousePress(int button, int state, int x, int y) {
    if (state == 1) {
        CameraDragMode mode = CameraDragMode::None;
        if (button == 0) mode = CameraDragMode::Rotate;
        else if (button == 1) mode = CameraDragMode::Pan;
        else if (button == 2) mode = CameraDragMode::Dolly;
        sCamera.BeginDrag(mode, x, y);
    } else {
        sCamera.EndDrag();
    }
}

void OnMouseMove(int x, int y) {
    sCamera.ApplyCursorDelta(x, y, sCanvasWidth, sCanvasHeight);
}

void OnMouseWheel(short delta) {
    sCamera.ApplyScroll((double)delta / 120.0);
}
