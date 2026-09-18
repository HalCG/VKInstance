# Vulkan Rendering Paths (渲染管线架构对比与技术实现指南)

本项目基于 Vulkan API 深入演示与对比了实时渲染中三种主流的渲染管线架构：**Forward (正向渲染)**、**Deferred (延迟渲染)** 以及 **Forward+ (Tile-Based Forward / 瓦片正向渲染)**。

项目支持在运行期通过键盘按键 (`1`/`2`/`3`) 无缝切换不同的渲染管线，通过 `[` 和 `]` 动态调节场景中的点光源数量（支持 64 / 256 / 1024 / 4096 / 10000 个动态点光源），并提供 G-Buffer 实时可视化调试 (`G` 键) 与 HDR 开关 (`H` 键)。

---

## 1. 关键技术原理与 Vulkan 实现细节

### 1.1 Forward Rendering (正向渲染)
* **原理**：标准的传统渲染管线。在绘制每一个网格物体 (Mesh) 时，Fragment Shader 对影响该物体的**所有光源**进行全量光照计算。
* **计算复杂度**：$\mathcal{O}(N_{\text{Objects}} \times N_{\text{Lights}})$
* **Vulkan 实现**：
  - 单 Pass 直接渲染至 Swapchain / Depth Target。
  - 将所有 Light 数据写入 SSBO / Uniform Buffer。
  - Fragment Shader 中 `for (int i = 0; i < lightCount; ++i)` 计算 Blinn-Phong / Cook-Torrance 光照。

### 1.2 Deferred Rendering (延迟渲染)
* **原理**：将“几何光栅化”与“光照计算”解耦成两个独立的阶段。
  - **Pass 1: Geometry Pass (G-Buffer 阶段)**：将场景中物体的几何属性（世界坐标 Pos、法线 Normal、漫反射 Albedo、镜面反射 Specular、深度 Depth）渲染输出到一组全屏多缓冲区 (G-Buffer Attachments)。
  - **Pass 2: Lighting Pass (光照阶段)**：绘制一个全屏 Quad，采样 G-Buffer 中的像素属性，仅对最终**遮挡测试成功**的屏幕像素点进行光源累加。
* **计算复杂度**：$\mathcal{O}(N_{\text{Objects}} + N_{\text{Pixels}} \times N_{\text{Lights}})$
* **Vulkan 实现**：
  - **G-Buffer Multi-Target RenderPass**：创建 `VK_FORMAT_R16G16B16A16_SFLOAT` (Pos / Normal) 和 `VK_FORMAT_R8G8B8A8_UNORM` (Albedo / Spec) 等多个 Color Attachment。
  - **Lighting Pass**：使用 Subpass / Pipeline 采样 G-Buffer Textures 执行全屏屏幕空间 Lighting Shader。
  - **G-Buffer 调试支持**：通过 `G` 键可在主画面中实时切出 4 宫格展示 Position、Normal、Albedo 与 Depth 分量。

### 1.3 Forward+ Rendering (Tile-Based Forward / 瓦片正向渲染)
* **原理**：结合了 Forward 的材质/透明度灵活性与 Deferred 的多光源高性能。
  - **Pass 1: Depth Prepass**：提前渲染场景几何深度到 Depth Buffer。
  - **Pass 2: Compute Shader Light Culling (瓦片光源裁切)**：将屏幕分割为 $16 \times 16$ 像素的瓦片网格 (Tiles)。Compute Shader 中每个 WorkGroup 负责一个 Tile，通过 Depth Buffer 计算当前 Tile 内像素的 Min/Max Depth，构建 Tile 视锥体 (Frustum)，裁切出相交的点光源列表，将光源 Index 列表写入 Light Grid SSBO。
  - **Pass 3: Forward Shading Pass**：使用正向渲染管线绘制几何体。Fragment Shader 根据当前像素的屏幕坐标 `(gl_FragCoord.xy)` 计算所在 Tile 索引，**仅遍历并计算该 Tile 内部重叠的局部光源**。
* **计算复杂度**：$\mathcal{O}(N_{\text{Objects}} + N_{\text{Tiles}} \times N_{\text{AvgLightsPerTile}})$
* **Vulkan 实现**：
  - **Vulkan Compute Pipeline**：编写 Light Culling Compute Shader (`cull.comp`)。
  - **SSBO (Storage Buffer)**：创建 `LightGridSSBO` 存储每个 Tile 的光源起始偏移与数量，`LightIndexSSBO` 存储紧凑的光源 ID 数组。
  - **Pipeline Barrier 屏障同步**：在 Compute Culling 之后插入 `vkCmdPipelineBarrier` (`VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT` $\to$ `VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT`) 保证 SSBO 内存可见性。

---

## 2. 三种渲染管线架构对比分析

| 特性维度 | Forward (正向渲染) | Deferred (延迟渲染) | Forward+ (瓦片正向) |
| :--- | :--- | :--- | :--- |
| **支持光源数量** | 极少 (< 32 点光源) | 海量 (> 10,000 点光源) | 海量 (> 10,000 点光源) |
| **计算复杂度** | $\mathcal{O}(\text{Obj} \times \text{Light})$ 爆炸 | $\mathcal{O}(\text{Obj} + \text{Pixel} \times \text{Light})$ | $\mathcal{O}(\text{Obj} + \text{Tile} \times \text{TileLight})$ |
| **透明物体/Alpha 支持** | 完美原生支持 | 不支持 (必须回退 Forward) | 完美原生支持 |
| **多材质 / Complex Shader** | 支持不同 Shader 材质 | 极受限 (G-Buffer 必须统一) | 支持不同 Shader 材质 |
| **硬件 MSAA 支持** | 原生支持 | 极困难 (G-Buffer 显存爆炸) | 原生支持 |
| **显存带宽开销** | 低 | 极高 (重度依赖 G-Buffer 带宽) | 低~中 (仅 Tile Grid SSBO) |
| **算法实现复杂度** | 简单 | 中等 (G-Buffer 管理) | 较高 (Compute Culling 屏障) |

---

## 3. 适用场景建议

1. **Forward Rendering**：
   - **适用场景**：VR 头显设备、移动端简单游戏、光源极少（< 10 个）的场景、透明/半透明物体密集（如烟雾、玻璃、水体）的场景。
   - **不适用**：拥有数百/数千个动态点光源的夜景、城市、地牢场景。

2. **Deferred Rendering**：
   - **适用场景**：海量动态点光源的 3A 游戏（如《赛博朋克 2077》、《刺客信条》）、高度依赖 G-Buffer 屏幕空间后处理（SSAO / SSR / SSGI / Deferred Decals）的引擎。
   - **不适用**：透明材质占比极高、移动端带宽受限设备、强依赖硬件 MSAA 的项目。

3. **Forward+ Rendering**：
   - **适用场景**：现代 3A 引擎（如《DOOM Eternal》、寒霜引擎 Frostbite）、既需要海量动态光源又包含大量半透明物体（头发、雨雪、烟雾、玻璃）的复杂场景、需要硬件 MSAA/TAA 结合的场景。
   - **不适用**：GPU 不支持 Compute Shader 的古老硬件。

---

## 4. 项目目录与文件结构

```
Vulkan_Rendering_Paths/
├── CMakeLists.txt                 # 项目 Build 脚本
├── doc/
│   └── performance_lag_fix_guide.md # 交换链重构与性能调优指南
├── include/
│   ├── DeferredRenderer.hpp       # Deferred Path (G-Buffer & Lighting) 渲染器
│   ├── ForwardPlusRenderer.hpp   # Forward+ Path (Compute Light Culling) 渲染器
│   ├── ForwardRenderer.hpp       # Forward Path 渲染器
│   ├── LightManager.hpp          # 动态光源生成与 SSBO 管理器
│   ├── PerfStats.hpp             # 帧率与阶段耗时分析统计器
│   └── RenderingPathsApp.hpp     # 主应用控制头文件
├── shaders/
│   ├── forward.vert / forward.frag   # Forward 阶段着色器
│   ├── gbuffer.vert / gbuffer.frag   # Deferred G-Buffer 几何着色器
│   ├── deferred.vert / deferred.frag # Deferred Lighting 全屏光照着色器
│   └── cull.comp                     # Forward+ Compute Light Culling 瓦片裁切着色器
└── src/
    ├── RenderingPathsApp.cpp     # 渲染路径切换逻辑与主循环
    └── main.cpp                  # 应用入口点
```

---

## 5. 编译与运行方式

### 编译
```bash
cmake --build <Build-Dir> --config Debug --target Vulkan_Rendering_Paths
```

### 快捷键操作
- **`1` / `F1`**：切换至 **Forward Path** (正向渲染)
- **`2` / `F2`**：切换至 **Deferred Path** (延迟渲染)
- **`3` / `F3`**：切换至 **Forward+ Path** (瓦片正向渲染)
- **`[` / `]`**：减少 / 增加场景动态点光源数量 (64 / 256 / 1024 / 4096 / 10000)
- **`G`**：切换 Deferred G-Buffer 4 宫格可视化调试
- **`H`**：切换 HDR 屏幕色调映射
- **鼠标左键拖拽**：旋转视角 (Orbit)
- **鼠标中键拖拽**：平移视角 (Pan)
- **鼠标右键拖拽/滚轮**：缩放视角 (Dolly)
