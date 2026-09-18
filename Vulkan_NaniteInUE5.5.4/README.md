# Nanite Vulkan Demo (0016)

OpenGL 版 `0015` 的 Vulkan 1.2 移植：相同 6-Pass GPU-Driven Nanite 管线（BVH 剔除 → ClusterCull → HW 光栅 → VisBuffer 可视化）。

## 构建

1. 安装 [Vulkan SDK](https://vulkan.lunarg.com/)（需 `glslc`）
2. 确保 `Res/mitsuba.bvh` 与 `Res/mitsuba.nanitemesh` 存在（或运行 `scripts\CopyRuntimeDeps.cmd` 从 0015 复制）
3. 运行 `build.bat`，生成 `NaniteVulkan.exe`

也可用 Visual Studio 打开 `NaniteVulkan.sln`。

## 文档

- [doc/Code_Review.md](doc/Code_Review.md) — 审查结论与已修复问题
- OpenGL 版详细阅读指南见 `../0015/doc/Code_Reading_Guide.md`

## 目录

```
src/App        入口
src/Scene      渲染管线编排
src/Platform   Vulkan 上下文与 Pipeline
src/Math       矩阵/四元数
src/Camera     Trackball 相机
src/Core       工具函数
Res/Shaders    GLSL 源码 → build.bat 编译为 .spv
```
