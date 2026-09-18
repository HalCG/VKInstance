# Vulkan Anti-Aliasing (抗锯齿技术对比与技术实现指南)

本项目基于 Vulkan API 深入演示与对比了实时渲染中四种主流的抗锯齿 (Anti-Aliasing, AA) 技术：**None (无抗锯齿)**、**MSAA (硬件多重采样抗锯齿)**、**FXAA (快速近似抗锯齿)** 以及 **TAA (时域抗锯齿)**。

---

## 1. 关键技术原理与 Vulkan 实现细节

### 1.1 None (无抗锯齿)
* **原理**：直接对场景像素点中心进行 1 次 Sampling（1x MSAA），无任何平滑处理。
* **Vulkan 实现**：
  - 直接渲染至 Swapchain 或 Single-Sample Offscreen Target。
  - `VkPipelineMultisampleStateCreateInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT`。

### 1.2 MSAA (Multisample Anti-Aliasing, 硬件多重采样抗锯齿)
* **原理**：在光栅化阶段对每个像素进行多点子采样（如 4x / 8x），仅在几何边缘（Primitive Topologies Coverage）重采样 Coverage Mask，而 Fragment Shader 依然针对每个像素只执行一次，兼顾几何边缘平滑与着色开销。
* **Vulkan 实现**：
  - **Multisample Attachments**：创建带有 `VK_SAMPLE_COUNT_4_BIT` (或最大可用采样数) 的 Transient Color & Depth Images。
  - **Resolve Attachment**：在 `VkRenderPass` 中配置 `pResolveAttachments` 指向单采样 Swapchain Image。在 RenderPass 结束时由 GPU 硬件自动将多采样颜色 Resolve 到单采样呈现目标。

### 1.3 FXAA (Fast Approximate Anti-Aliasing, 快速近似抗锯齿)
* **原理**：基于屏幕空间图像后处理（Screen-space Post-Processing）。片段着色器通过提取 RGB 颜色的亮度 (Luminance / Luma)，检测高对比度边缘，计算边缘梯度方向与斜率，沿着垂直方向使用双线性插值进行颜色混合。
* **Vulkan 实现**：
  - **Pass 1**：正向渲染场景至 Offscreen Color & Depth Buffer。
  - **Pass 2 (FXAA Post-Pass)**：绘制全屏 Triangle/Quad，FXAA Fragment Shader 采样 Offscreen Color Texture。
  - **Luma 计算**：`float luma = dot(color, vec3(0.299, 0.587, 0.114));`
  - **插值**：根据 3x3 邻域 Luma 极差确定边缘斜率与距离，按像素 Offset 插值。

### 1.4 TAA (Temporal Anti-Aliasing, 时域抗锯齿)
* **原理**：利用时间维度的连续性，将抗锯齿开销分散在多帧中。每一帧在投影矩阵中施加微小的子像素偏移（Subpixel Jitter），并在后处理阶段利用相机逆矩阵/上一帧投影矩阵重投影（Reprojection）采样历史帧，通过 EMA (Exponential Moving Average) 混合与 3x3 邻域颜色裁切 (Neighborhood Clamping) 消除锯齿与拖影。
* **Vulkan 实现**：
  - **Subpixel Camera Jitter**：使用 Halton(2,3) 8-Sample 采样序列，在当前帧 `proj[2][0]` 与 `proj[2][1]` 矩阵元素中施加微像素级 Offset。
  - **相机重投影 (Camera Motion Reprojection)**：
    $$\text{WorldPos} = (\text{ViewProj}_{\text{curr}})^{-1} \cdot \text{NDCPos}_{\text{curr}}$$
    $$\text{NDCPos}_{\text{prev}} = \text{ViewProj}_{\text{prev}} \cdot \text{WorldPos}$$
  - **YCoCg 空间 3x3 Neighborhood Color Clamping**：
    将 RGB 转换至 YCoCg 颜色空间。提取当前像素周围 3x3 邻域的 AABB 包围盒 $[YCoCg_{\min}, YCoCg_{\max}]$，将采样到的历史颜色强制 Clamp 在包围盒内。**彻底消除鬼影 (Ghosting)**。
  - **Ping-Pong Buffer**：双历史纹理 `historyTex_[2]` 轮换累积。

---

## 2. 四种抗锯齿技术对比分析

| 特性维度 | None | MSAA (4x/8x) | FXAA | TAA |
| :--- | :--- | :--- | :--- | :--- |
| **几何边缘锯齿** | 严重 | 极佳 | 较好 | 极佳 |
| **纹理/着色锯齿** | 无改善 | 无改善（需 SSAA）| 微弱改善 | 极佳（消除亚像素闪烁）|
| **亚像素几何** | 丢失/断裂 | 部分保留 | 丢失/模糊 | 完整保留 |
| **画面清晰度** | 绝对锐利 | 极度锐利 | 略微偏软 | 适度平滑 |
| **动态鬼影** | 无 | 无 | 无 | 低（通过 YCoCg Clamping 抑制）|
| **显存额外开销** | 0 | 高 (多倍 Color+Depth) | 低 (1张 Offscreen Color) | 中 (2张 History Textures) |
| **GPU 算力开销** | 极低 | 中~高 | 极低 | 低~中 |
| **Deferred 兼容性** | 完美 | 极困难 | 完美 | 完美 |

---

## 3. 适用场景建议

1. **MSAA**：正向渲染架构、CAD / 3D 工业建模设计软件、移动端 Tile-Based GPU。
2. **FXAA**：中低端移动设备、竞技类 FPS 游戏、Deferred 基础低成本 AA。
3. **TAA**：现代 3A 级 PBR 引擎、Deferred 渲染管线、包含光线追踪/降噪的管线。
