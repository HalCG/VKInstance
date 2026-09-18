# VkInstance

Vulkan 图形学 Demo 集合（与 [01_GLInstance](../01_GLInstance) 同级）。每个子项目为独立 Vulkan 示例程序。

## 子项目一览

| 子项目 | 主题 | 状态 |
|--------|------|------|
| `Vulkan_Common` | VulkanContext / Buffer / Shader 共享基础组件 | 可用 |
| `Vulkan_DemoCommon` | DemoApp / DemoRhi / DemoMesh 共享演示框架 | 可用 |
| `Vulkan_Anti_Aliasing` | None / MSAA / FXAA / TAA 4种抗锯齿对比 | **可交互** |
| `Vulkan_Rendering_Paths` | Forward / Deferred / Forward+ 渲染路径对比 | **可交互** |
| `Vulkan_OIT_Depth_Peeling` | 顺序无关透明 · 深度剥离 | **可交互** · [文档](Vulkan_OIT_Depth_Peeling/README.md) |
| `Vulkan_OIT_Linked_list` | 顺序无关透明 · 链表 (Storage Buffer) | **可交互** |
| `Vulkan_OIT_Stochastic_Transparency` | 顺序无关透明 · 随机透明度 | **可交互** |
| `Vulkan_NaniteInUE5.5.4` | GPU-Driven Nanite 6-Pass 管线 | 独立构建，见子目录 README |

## 环境要求

- **系统：** Windows x64
- **编译器：** MSVC 2022+ 或 Clang（见 `CMakePresets.json`）
- **构建：** CMake ≥ 3.10
- **Vulkan SDK：** 需安装并包含 `glslc`（[下载](https://vulkan.lunarg.com/)）
- **第三方依赖：** 复用同级 OpenGL 工作区的 `modules/`（GLFW、GLM 等）

目录布局示例：

```
opengl/
├── 01_GLInstance/          # OpenGL 参考工程（提供 modules/）
└── 11_VkInstance/          # 本仓库
```

若 `01_GLInstance` 不在同级，可将 `modules/` 复制到本仓库根目录。

## 构建

**构建说明见 [doc/BUILD.md](doc/BUILD.md)**。代码审查与优化待办见 [doc/CODE_REVIEW_TODO.md](doc/CODE_REVIEW_TODO.md)。

```bash
cd 11_VkInstance
cmake --preset x64-msvc-2026
cmake --build out/build/x64-msvc-2026 --config Debug
```

## 运行

编译后程序在 **`run/Debug/<项目名>/`** 或 **`run/Release/<项目名>/`**（与 `--config` 一致）：

```text
run/Debug/Vulkan_OIT_Depth_Peeling/Vulkan_OIT_Depth_Peeling.exe
run/Release/Vulkan_Rendering_Paths/Vulkan_Rendering_Paths.exe
```

exe、DLL、`resources/` 已在同一目录；IDE 调试工作目录亦指向此处。

### Vulkan_Rendering_Paths 操作

| 按键 | 功能 |
|------|------|
| `1` / `F1` | Forward 前向渲染 |
| `2` / `F2` | Deferred 延迟渲染 |
| `3` / `F3` | Forward+（CPU Tile 光源剔除 + 按 Tile 着色） |
| `[` / `]` | 切换光源数量 preset（64/128/256/512） |
| `G` | Deferred 路径下切换 GBuffer 调试视图 |
| `H` | 切换 HDR Tone Mapping（Deferred） |
| 鼠标 | 左键旋转 / 中键平移 / 右键缩放 / 滚轮缩放 |

窗口标题约每 15 帧刷新 FPS / 各 Pass 耗时；终端约每 120 帧输出 CSV 性能日志。程序持续渲染（非静止单帧）。

### Vulkan_Anti_Aliasing 操作

| 按键 | 功能 |
|------|------|
| `1` | 无抗锯齿 (None) |
| `2` | 硬件 MSAA（4x/8x 多重采样） |
| `3` | FXAA（屏幕空间快速近似抗锯齿） |
| `4` | TAA（时域重投影 + YCoCg 邻域裁剪抗锯齿） |

### OIT / 顺序无关透明 Demo

| 子项目 | 方案 | 文档 |
|--------|------|------|
| `Vulkan_OIT_Depth_Peeling` | 深度剥离 + Front-to-Back 累积混合 | [README](Vulkan_OIT_Depth_Peeling/README.md) |
| `Vulkan_OIT_Linked_list` | Storage Buffer 原子链表 + 全屏 Resolve | （待补充） |
| `Vulkan_OIT_Stochastic_Transparency` | 随机 discard 近似透明 | （待补充） |

**Vulkan_OIT_Depth_Peeling 操作：** 鼠标拖拽旋转 / 滚轮缩放 / 左右方向键旋转。

共享混合工具：`Vulkan_DemoCommon` 中 `DemoRhi::PipelineInfo::oitFrontToBackBlend`（对应 OpenGL `glBlendFuncSeparate(GL_DST_ALPHA, GL_ONE, ...)`）。

## Nanite 子项目

`Vulkan_NaniteInUE5.5.4` 使用独立 MSVC 方案，未纳入根 CMake：

```bash
cd Vulkan_NaniteInUE5.5.4
build.bat
```

详见 [Vulkan_NaniteInUE5.5.4/README.md](Vulkan_NaniteInUE5.5.4/README.md)。

## 设计约定（对齐 OpenGL 版）

- Shader 在 **CMake 构建期** 由 `glslc` 编译为 SPIR-V，部署到 `resources/shaders/`
- 运行时 CWD = 可执行文件目录
- 共享依赖通过 `../01_GLInstance/modules` 或本地 `modules/` 解析

## 🚀 路线图与后续 Demo 规划 (Roadmap & TODO)

围绕 Deferred Rendering (延迟渲染) G-Buffer 数据管线，规划新增以下 3 个高级屏幕空间技术 Demo：

- [ ] **`Vulkan_SSAO` (Screen Space Ambient Occlusion 屏幕空间环境光遮蔽)**
  - 基于 G-Buffer 深度图 (Depth) 与法线图 (Normal) 进行法线半球 (Hemisphere) 随机采样。
  - 动态计算角落、接缝遮挡因子，模拟柔和的真实缝隙阴影。

- [ ] **`Vulkan_SSR` (Screen Space Reflection 屏幕空间反射)**
  - 利用视角向量与法线计算反射光线，在 G-Buffer 深度图上进行 Ray Marching 光线步进。
  - 实时渲染水面、湿滑地面和大理石金属性反射。

- [ ] **`Vulkan_Deferred_Decals` (屏幕空间延迟贴花)**
  - 放置 3D Decal Box，在 Fragment Shader 中反投影像素世界坐标。
  - 在不修改物体网格的前提下，批量将弹痕、血迹、脚印与涂鸦“印”在几何体表面。

