# Vulkan Rendering Paths (渲染管线架构对比与技术实现指南)

本项目基于 Vulkan API 深入演示与对比了实时渲染中三种主流的渲染管线架构：**Forward (正向渲染)**、**Deferred (延迟渲染)** 以及 **Forward+ (Tile-Based Forward / 瓦片正向渲染)**。

---

## 1. 关键技术原理与 Vulkan 实现细节

### 1.1 Forward Rendering (正向渲染)
* **原理**：标准的传统渲染管线。在绘制每一个网格物体 (Mesh) 时，Fragment Shader 对影响该物体的**所有光源**进行全量光照计算。
* **计算复杂度**：$\mathcal{O}(N_{\text{Objects}} \times N_{\text{Lights}})$
* **Vulkan 实现**：
  - 单 Pass 直接渲染至 Swapchain / Depth Target。
  - 所有 Light 数据写入 SSBO / Uniform Buffer，循环计算光照。

### 1.2 Deferred Rendering (延迟渲染)
* **原理**：将“几何光栅化”与“光照计算”解耦成两个独立的阶段。
  - **Pass 1: Geometry Pass (G-Buffer 阶段)**：将物体几何属性（Pos / Normal / Albedo / Specular / Depth）输出到 G-Buffer 多缓冲区。
  - **Pass 2: Lighting Pass (光照阶段)**：全屏 Quad 采样 G-Buffer，仅对**遮挡测试成功**的屏幕像素做光源累加。
* **计算复杂度**：$\mathcal{O}(N_{\text{Objects}} + N_{\text{Pixels}} \times N_{\text{Lights}})$
* **Vulkan 实现**：
  - G-Buffer 多 Attachment 绑定（Pos / Normal 为 `R16G16B16A16_SFLOAT`，Albedo / Spec 为 `R8G8B8A8_UNORM`）。
  - 按 `G` 键可调出 4 宫格全屏 G-Buffer 分量可视化调试。

### 1.3 Forward+ Rendering (Tile-Based Forward / 瓦片正向渲染)
* **原理**：结合了 Forward 的材质/透明度灵活性与 Deferred 的多光源高性能。
  - **Pass 1: Depth Prepass**：提前渲染场景几何深度到 Depth Buffer。
  - **Pass 2: Compute Shader Light Culling**：屏幕分割为 $16 \times 16$ 像素 Tiles。Compute Shader 依据 Tile Min/Max Depth 切片裁切 Point Lights，输出 Tile Light Index List 与 Grid SSBO。
  - **Pass 3: Forward Shading Pass**：正向渲染 Shader 依据当前像素 Tile 索引，**仅遍历与计算 Tile 局部光源**。
* **计算复杂度**：$\mathcal{O}(N_{\text{Objects}} + N_{\text{Tiles}} \times N_{\text{AvgLightsPerTile}})$
* **Vulkan 实现**：
  - Compute Shader (`cull.comp`) 构建 Tile 视锥体。
  - Pipeline Barrier 同步控制（`COMPUTE_SHADER_BIT` $\to$ `FRAGMENT_SHADER_BIT`）。

---

## 2. 三种渲染管线架构对比分析

| 特性维度 | Forward (正向渲染) | Deferred (延迟渲染) | Forward+ (瓦片正向) |
| :--- | :--- | :--- | :--- |
| **支持光源数量** | 极少 (< 32 点光源) | 海量 (> 10,000 点光源) | 海量 (> 10,000 点光源) |
| **计算复杂度** | $\mathcal{O}(\text{Obj} \times \text{Light})$ | $\mathcal{O}(\text{Obj} + \text{Pixel} \times \text{Light})$ | $\mathcal{O}(\text{Obj} + \text{Tile} \times \text{TileLight})$ |
| **透明物体/Alpha 支持** | 完美原生支持 | 不支持 (需回退 Forward) | 完美原生支持 |
| **多材质 / Complex Shader** | 支持不同 Shader 材质 | 极受限 (G-Buffer 必须统一) | 支持不同 Shader 材质 |
| **硬件 MSAA 支持** | 原生支持 | 极困难 (G-Buffer 显存爆炸) | 原生支持 |
| **显存带宽开销** | 低 | 极高 (重度依赖 G-Buffer 带宽) | 低~中 |

---

## 3. 适用场景建议

1. **Forward**：VR 头显、移动端简单游戏、光源极少（< 10 个）的场景、透明物体密集场景。
2. **Deferred**：海量动态点光源 3A 游戏、重度依赖 G-Buffer 后处理 (SSAO/SSR/Decals) 的引擎。
3. **Forward+**：现代 3A 引擎（如《DOOM Eternal》）、既需要海量动态光源又包含大量半透明物体与硬件 MSAA/TAA 的场景。
