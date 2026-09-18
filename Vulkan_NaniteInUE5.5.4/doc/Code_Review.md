# Nanite Vulkan (0016) 代码审查与修复记录

对照 OpenGL 版 `0015` 的 GPU 管线逻辑，对另一 AI 生成的 Vulkan 移植版进行审查。

## 管线逻辑（与 0015 一致）

```
RasterClear → NodeAndClusterCull×4 → ClusterCull → HWRasterize → Visualization → FSQ
```

算法层（BVH 四叉遍历、LODScale、`VisBuffer64` atomicMin、间接绘制 384×instanceCount）与 0015 相同，**核心 Shader 逻辑无本质偏差**。

## 已发现并修复的问题

| 严重度 | 问题 | 修复 |
|--------|------|------|
| **高** | HWRasterize 使用 FSQ 的 Swapchain Framebuffer，但 RenderPass 不兼容（load/store/finalLayout 不同） | HWRasterize 使用**离屏颜色附件 + 独立 Framebuffer** |
| **高** | 未启用 `shaderBufferInt64Atomics`，Fragment `atomicMin(uint64_t)` 可能失败 | `VkPhysicalDeviceVulkan12Features::shaderBufferInt64Atomics` |
| **中** | 图形管线未设置 `pDepthStencilState` | 显式关闭深度测试/写入 |
| **中** | Shader 硬编码 `1280×720` 像素索引 | `GlobalConstants.Misc0.xy` = 画布宽高 |
| **中** | Visualization → FSQ 缺少 Image Barrier | Compute→Fragment 同步 |
| **低** | 资源文件加载失败静默崩溃 | `LoadFileContent` 打日志 + `InitScene` 抛异常 |
| **低** | 相机/模型与 0015 不一致 | 对齐初始 eye、zNear、X 轴 180° 旋转 |

## 仍待改进（未阻塞 Demo）

- 未启用 Khronos Validation Layer（建议 Debug 打开）
- 单帧 in-flight（单 Fence），无 per-swapchain-image 资源
- Compute 队列已创建但未单独使用（全走 Graphics 队列）
- `GetClusterInfo` 等在多个 Shader 重复，可抽 `NaniteCommon.glsl`
- 无 SW 光栅化路径（与 0015 相同）
- `LODScaleHW` 仍为布局占位

## 目录结构（目标，与 0015 对齐）

```
0016/
  src/App/          main.cpp
  src/Scene/        scene.*
  src/Platform/     vulkan_*
  src/Math/         matrix4, float4, quaternion
  src/Camera/       trackball_camera
  src/Core/         utils
  Res/Shaders/      *.comp / *.vert / *.frag → build.bat 编译 .spv
  scripts/          CopyRuntimeDeps.cmd
  doc/              本文档 + 阅读指南
```

## 构建

```bat
scripts\CopyRuntimeDeps.cmd   REM 若 Res 下无 bvh/nanitemesh
build.bat                     REM glslc + cl.exe
```

需安装 Vulkan SDK（`glslc`、`vulkan-1.lib`）。
