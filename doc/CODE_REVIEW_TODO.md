# 代码审查与优化待办 (Code Review TODO)

> **审查范围**（2026-09）：`Vulkan_Common`、`Vulkan_DemoCommon`、`Vulkan_Anti_Aliasing`、`Vulkan_Rendering_Paths`、`Vulkan_OIT_Depth_Peeling`、`Vulkan_OIT_Linked_list`、`Vulkan_OIT_Stochastic_Transparency`  
> **不在范围**：`Vulkan_NaniteInUE5.5.4`（保持独立 `build.bat` / `NaniteVulkan.sln`）  
> **状态**：仅记录结论，实施留待后续；完成项将 `[ ]` 改为 `[x]`。

构建与运行约定见 [BUILD.md](BUILD.md)。新 Demo 路线图见 [roadmap_and_todo.md](roadmap_and_todo.md)。

---

## 审查摘要

| 项目 | 角色 | 主要问题 |
|------|------|----------|
| `Vulkan_Common` | 设备 / Buffer / Shader 基础库 | 上传路径大量 `vkQueueWaitIdle`；缺 RAII |
| `Vulkan_DemoCommon` | DemoApp 帧循环 + DemoRhi | 与 `VulkanRhi` 重复；描述符工厂冗长 |
| `Vulkan_Anti_Aliasing` | 抗锯齿 Demo | 缺 `resources/`；TAA 每帧更新描述符；单体文件过大 |
| `Vulkan_Rendering_Paths` | 三条渲染路径 | 内部分层好，但未接入 DemoCommon，重复 ~1000+ 行 |
| OIT 三件套 | 算法各异 | CMake 一致；`OitScene` / `OrbitCamera` / 公共 shader 三份复制 |

**最大结构性债务**：`VulkanRhi` ≈ `DemoRhi` 子集；`RenderingPathsApp` ≈ `DemoApp` 副本；三个 OIT 各维护一份场景加载代码。

---

## P0 — Bug 与明显不一致（优先）

- [ ] **Stochastic 滚轮缩放无效**  
  `Vulkan_OIT_Stochastic_Transparency/include/OrbitCamera.hpp`：`eye()` 使用 `AppConfig::kCameraOrbitRadius` 常量，未用成员 `orbitRadius`（Peeling/Linked 写法正确）。

- [ ] **Linked List 合成背景色**  
  Pass 1 不透明 RT 清黑；`composite.frag` 未像 Depth Peeling 那样混入 `backgroundColor()`，空白区域偏黑。

- [ ] **Anti_Aliasing 缺少 `resources/`**  
  `vk_ws_deploy_static_resources` 无目录则跳过；`spot.obj` 从未部署，运行时一直走 fallback 立方体。补 `resources/models/spot/`（可从 OIT / Rendering_Paths 复制）。

- [ ] **OIT README / main 运行路径过时**  
  仍写 `out/build/.../bin/Debug/` 或子目录 `build/`；应统一为 `run/Debug/<项目>/`、`run/Release/<项目>/`（见 BUILD.md）。

- [ ] **Stochastic 方向键与兄弟 Demo 相反**  
  `StochasticDemo.cpp` 左右键增减角度方向与 Peeling/Linked 不一致（低优先级 UX）。

- [ ] **Stochastic `main.cpp` 未注册 `setKeyCallback`**  
  依赖 `render()` 内轮询，与其它 OIT 不一致。

---

## P1 — 架构与代码复用（高 ROI）

### 共享模块

- [ ] **抽取 OIT 公共代码** → `Vulkan_OIT_Common` 或并入 `Vulkan_DemoCommon`  
  - `OitScene.cpp`（~280 行 × 3）  
  - `OrbitCamera.hpp`（~90 行 × 3）  
  - `main.cpp` DemoApp 接线（~45 行 × 3）  
  - 保留差异：场景布局枚举（Peeling vs Stochastic）、`kInitialOrbitAngle`、算法常量。

- [ ] **统一 OitScene 行为**  
  - Spot 纹理 fallback（白纹理 vs 灰纹理）  
  - 无 UV 时默认 `vec2(0)`（Stochastic 缺 `else` 分支）  
  - Assimp 加载失败日志（仅 Linked List 有 `cerr`）

- [ ] **共享 GLSL 片段**  
  `blinn_phong.glsl`、`lit.vert`、`fullscreen.vert` 通过 `#include` + `vk_ws_compile_shaders` 复用。

- [ ] **Rendering_Paths 接入 `Vulkan_DemoCommon`**  
  - `CMakeLists.txt` 链接 `Vulkan_DemoCommon`  
  - 删除或大幅瘦身 `VulkanRhi.cpp` / `VulkanRhi.hpp`  
  - 帧循环迁到 `DemoApp` + `setFrameCallback`；保留 Forward/Deferred/Forward+ 分层

- [ ] **统一场景加载**  
  将 `uploadMesh`、Assimp 加载、纹理 fallback 从 `AntiAliasingDemo.cpp`、`Scene.cpp`、三个 `OitScene.cpp` 收敛到 `DemoMesh` 或新 `DemoScene`。

- [ ] **统一 `stb_image` 链接**  
  全工作区使用 `stb_image_wrap.cpp` 单 TU；移除 AA / Rendering_Paths 中的 `#define STB_IMAGE_IMPLEMENTATION`。

### 构建与仓库卫生

- [ ] **删除子项目内残留 `build/`**  
  `Vulkan_OIT_Linked_list/build/`、`Vulkan_OIT_Stochastic_Transparency/build/`、`Vulkan_Rendering_Paths/build/`（旧独立 configure 失败产物）。

- [ ] **补充共享纹理资源**  
  各 Demo 缺 `spot.png`；可考虑工作区级 `resources/models/spot/spot.png` 或复制到各子项目 POST_BUILD 部署。

- [ ] **更新 Depth Peeling README 性能描述**  
  「无每帧 Descriptor 重建」不准确：每层仍 `writePeelDescriptor` 更新 depth 绑定（`DepthPeelingDemo.cpp`）。

---

## P2 — 性能（Demo 可接受，扩展前需改）

### Vulkan_Common / Vulkan_DemoCommon（影响全部 Demo）

- [ ] **去掉上传路径上的 `vkQueueWaitIdle`**  
  `VulkanBuffer.cpp::copyBuffer`、`transitionImageLayout`；`DemoRhi::endSingleTimeCommands` → fence + 单次 submit。

- [ ] **批量 mesh 上传**  
  `DemoMesh::uploadMesh`：单 staging buffer、vertex+index 一次 submit；可选持久 staging 池。

- [ ] **缓存 `findDepthFormat()`**  
  在 `VulkanContext` 初始化时算一次，替换 `DemoApp` / `DemoRhi` 多处重复查询。

- [ ] **独立 transfer command pool**  
  上传与帧渲染 command pool 分离（`VK_COMMAND_POOL_CREATE_TRANSIENT_BIT`）。

- [ ] **合并 barrier 工具**  
  `VulkanUtil::transitionImageLayout` 与 `DemoRhi::cmdTransitionImage` 统一为 Common 层一套 API。

- [ ] **内存子分配（VMA 或自研）**  
  减少每 buffer/image 单独 `vkAllocateMemory` 的碎片与 teardown 成本。

- [ ] **按需开启设备特性**  
  `fragmentStoresAndAtomics` 仅 OIT 需要；`samplerAnisotropy` 应先查询 feature bit。

- [ ] **为所有 render pass 补 subpass dependency**  
  目前仅 `createSwapchainRenderPass` 有外部依赖；offscreen / MSAA / peel 等缺失。

### Vulkan_OIT_Depth_Peeling

- [ ] **深度 ping-pong：2 套 descriptor set 切换**  
  减少每层 × 每物体 `vkUpdateDescriptorSets`（约 40 次/帧）。

- [ ] **不透明物体只画一次**  
  Spot 等 alpha≈1 物体不必每层 peel 重绘。

- [ ] **Occlusion query / 空层 early-out**  
  OpenGL 版有；当前固定 10 层（`kMaxDepthPeelLayers`）。

### Vulkan_OIT_Linked_list

- [ ] **SSBO 节点池上限与分辨率解耦**  
  `maxNodes = W×H×20`：1080p 可超 1GB；加可配置 ceiling 或按 Demo 分辨率固定策略。

- [ ] **shader `MAX_FRAGMENTS` 与 `AppConfig::kMaxFragmentsPerPixel` 对齐**  
  当前 composite 中为 75，配置为 20。

- [ ] **Composite 背景与 Depth Peeling 行为对齐**（亦见 P0）。

### Vulkan_OIT_Stochastic_Transparency

- [ ] **`frameID` 按真实帧计数**  
  当前按 `modelCnt` 循环，时域噪声几乎静态；可选 TAA 钩子。

- [ ] **修复 `OrbitCamera::eye()`**（亦见 P0）。

### Vulkan_Anti_Aliasing

- [ ] **TAA：init/resize 时绑定描述符，帧间只改 push constant**  
  避免 `writeTaaDescriptorSet` 每帧 `vkUpdateDescriptorSets`（`AntiAliasingDemo.cpp:1091`）。

- [ ] **Spot 实例化绘制**  
  9 次相同 mesh draw → instance buffer 或合并 draw。

- [ ] **拆分 `AntiAliasingDemo.cpp`（~1170 行）**  
  建议：`AaScene` / `AaTargets` / `AaPipelines` / 模式分发。

### Vulkan_Rendering_Paths

- [ ] **按当前路径懒加载 renderer**  
  启动时不 init 全部三条路径的 G-Buffer / 管线 / 描述符池。

- [ ] **Forward / Deferred Pass2 光照扩展**  
  当前 O(像素 × 512 灯)；长期需 tiled / clustered lights。

- [ ] **Forward+ CPU tile culling 瓶颈**  
  `LightManager::buildForwardPlusTiles` 每帧 512 灯投影；可考虑 GPU compute。

- [ ] **Spot 实例化**（12 实例，`AppConfig::kSpotInstanceCount`）。

- [ ] **移除 Deferred 冗余 barrier**（若 render pass dependency 已足够）  
  `DeferredRenderer.cpp` Pass 后多条 `vkCmdPipelineBarrier`。

---

## P3 — 工程质量与小项

### Vulkan_Common

- [ ] `VK_WS_GLSLC_EXECUTABLE` 改为 PRIVATE（避免 PUBLIC 传播绝对路径）。
- [ ] GLFW 对 `Vulkan_Common` 改为 PRIVATE link；窗口与设备接口可拆分。
- [ ] `pickPhysicalDevice` 优先独显 / 按 VRAM 评分。
- [ ] Debug 下校验层可用性检查；`debugCallback` 可考虑 `VK_TRUE` 断点。
- [ ] `readFile` / SPIR-V `codeSize` 对齐检查；`compileGlslToSpirv` 捕获 stderr。
- [ ] 轻量 RAII：`VulkanBuffer`、`VulkanImage`、`VulkanShaderModule`（move-only）。

### Vulkan_DemoCommon

- [ ] `DemoRhi` 12 个 `create*DescriptorLayout` → `DescriptorLayoutBuilder`。
- [ ] 删除 `VulkanContext::swapChainFramebuffers_` 死字段或实现一致 API。
- [ ] `DemoApp::run`：`VK_SUBOPTIMAL_KHR` 处理；检查 `vkQueueSubmit` / `vkBeginCommandBuffer` 返回值。
- [ ] FPS `std::cout` 用 `#ifndef NDEBUG` 或运行时开关。
- [ ] `DemoApp::destroyTargets` 重复 `imagesInFlight_.clear()`（无害，可删一行）。
- [ ] `writeMeshDescriptor` 与 layout binding 2 文档化（fragment UBO 由调用方单独写）。

### Vulkan_Anti_Aliasing

- [ ] `resourcePath()` 迁到 `AppConfig` 式头文件（与 Rendering_Paths 一致）。
- [ ] `onResize` 尽量避免全 `vkDeviceWaitIdle`（与 in-flight fence 协调）。

### Vulkan_Rendering_Paths

- [ ] `ForwardRenderer` / `DeferredRenderer` / `ForwardPlusRenderer` 中 `meshBindings()` / `meshAttributes()` 去重。
- [ ] `AppConfig::shaderPath()` 推广到其它 Demo 的硬编码 `resources/shaders/...`。

### CMake

- [ ] 子目录 `CMakeLists.txt` 可去掉重复 `project()`（可选整洁项）。
- [ ] 库目标增加 `/W4` 或统一 warning 策略（可选）。

---

## 建议实施顺序（参考）

若分批落地，推荐：

1. **P0 全部**（小改动、用户可感知）  
2. **P1：OIT 公共模块 + AA resources + 文档路径**  
3. **P1：Rendering_Paths → DemoCommon**（最大减重复）  
4. **P2：Common 上传/同步优化**（惠及所有 Demo）  
5. **P2：各 Demo 算法向优化**  
6. **P3** 随重构顺带处理  

---

## 不在本清单内

| 项 | 说明 |
|----|------|
| `Vulkan_NaniteInUE5.5.4` | 保持独立构建，不纳入根 CMake / `run/` 约定 |
| 新 Demo（SSAO / SSR / Decals） | 见 [roadmap_and_todo.md](roadmap_and_todo.md) |
| `run/Debug` vs `run/Release` 分目录 | **已完成**（见 BUILD.md） |

---

## 变更记录

| 日期 | 说明 |
|------|------|
| 2026-09-18 | 初版：全项目审查结论归档为待办清单 |
