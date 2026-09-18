#pragma once

#include "vulkan_context.h"
#include "vulkan_utils.h"
#include "vulkan_pipeline.h"
#include "matrix4.h"
#include "float4.h"
#include "trackball_camera.h"

// ==============================================================================
// 全局 Uniform 常量数据结构 (对应 Shader 中的 GlobalConstants)
// 包含透视/视图/模型矩阵、相机位置、视线方向以及 LOD 缩放比例
// ==============================================================================
struct GlobalConstants {
	float ProjectionMatrix[16];   // 4x4 透视投影矩阵
	float ViewMatrix[16];         // 4x4 视图转换矩阵
	float ModelMatrix[16];        // 4x4 模型变换矩阵
	uint32_t Misc0[4];            // x=画布宽, y=画布高（Shader 像素索引用）；z/w 预留
	float CameraPositionWS[4];    // xyz: 相机世界坐标; w: LODScale（Pass2/3 实际参与 LOD 判据）
	float ViewDirectionWS[4];     // xyz: 相机前向; w: LODScaleHW（布局占位，当前未用）
};

// 场景与管线初始化
void InitScene(VulkanContext* ctx, uint32_t width, uint32_t height);

// 核心单帧渲染循环 (执行 GPU 剔除、硬件间接绘制、VisBuffer 解码与 Swapchain 呈现)
void RenderOneFrame(VulkanContext* ctx, float frameTime);

// 资源清理
void CleanupScene();

// 键盘/鼠标交互回调函数
void OnKeyUp(WPARAM wParam);
void OnMousePress(int button, int state, int x, int y);
void OnMouseMove(int x, int y);
void OnMouseWheel(short delta);
