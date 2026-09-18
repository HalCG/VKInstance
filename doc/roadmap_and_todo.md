# Vulkan 拓展 Demo 路线图与设计规划 (Roadmap & TODO)

为了进一步丰富 Vulkan 工作区的技术沉淀，围绕 **Deferred Rendering (延迟渲染) G-Buffer 数据管线**，规划后续新增以下 3 个高价值屏幕空间技术示例程序：

---

## 1. `Vulkan_SSAO` (屏幕空间环境光遮蔽 Demo)

### 1.1 技术目标
- 在场景角落、凹槽、物件缝隙处计算暗部遮挡阴影，解决无环境阴影导致的画面“塑料感”与视觉浮空问题。

### 1.2 核心实现设计
1. **Pass 1: Geometry Pass**：输出场景 Pos、Normal、Depth 至 G-Buffer。
2. **Pass 2: SSAO Compute/Graphics Pass**：
   - 随机生成 32/64 个半球采样点核 (Sample Kernel) 与 $4 \times 4$ 随机旋转纹理。
   - 在片段着色器中，以像素当前 Depth 和 Normal 构建 TBN 矩阵，将半球采样点变换至世界/视图空间。
   - 采样周边像素的深度进行遮挡判断 (Depth Test)，累加计算遮挡因子 $AO \in [0.0, 1.0]$。
3. **Pass 3: SSAO Blur Pass**：采用 $4 \times 4$ 双向导向滤波 (Bilateral Blur) 消除随机采样噪声，保持边缘清晰。
4. **Pass 4: Final Composition**：将 Blur 后的 AO 因子乘到环境光成分上。

---

## 2. `Vulkan_SSR` (屏幕空间反射 Demo)

### 1.1 技术目标
- 在水面、雨后湿滑地面、大理石地板和金属性表面呈现平滑/粗糙的镜面场景倒影。

### 1.2 核心实现设计
1. **Pass 1: Geometry Pass**：输出 Pos、Normal、Albedo、Roughness、Metallic 至 G-Buffer。
2. **Pass 2: SSR Ray Marching Pass**：
   - 根据视角向量 $\mathbf{V}$ 和法线向量 $\mathbf{N}$ 计算反射向量 $\mathbf{R} = \text{reflect}(-\mathbf{V}, \mathbf{N})$。
   - 在屏幕空间（结合 Depth Map）沿着 $\mathbf{R}$ 方向进行光线步进 (Screen Space Ray Marching / Hi-Z Ray Tracing)。
   - 判断步进点与深度图表面相交，采样交点处的主颜色纹理。
3. **Pass 3: Reflection Blend**：根据粗糙度 (Roughness) 对反射画面进行 Mipmap / Hi-Z 模糊，按 Fresnel 菲涅尔系数与金属度混合到主画面。

---

## 3. `Vulkan_Deferred_Decals` (屏幕空间延迟贴花 Demo)

### 3.1 技术目标
- 在不破坏或修改任何已有物体网格/材质贴图的前提下，在任意复杂表面上批量动态绘制弹痕、血迹、脚印、墙面涂鸦与污渍。

### 3.2 核心实现设计
1. **Pass 1: Base Geometry Pass**：渲染基础场景至 G-Buffer。
2. **Pass 2: Decals Rendering Pass**：
   - 在贴花位置绘制一个代表贴花影响范围的 3D Box 几何体 (Decal Box)。
   - 在 Decal Fragment Shader 中读取当前屏幕像素的 G-Buffer 世界坐标 $\mathbf{P}_{\text{world}}$。
   - 将 $\mathbf{P}_{\text{world}}$ 乘以 Decal Box 的逆 Model 矩阵 $\mathbf{M}_{\text{decal}}^{-1}$，转换至 Decal 局部空间。
   - 检查坐标是否在区间 $[-0.5, 0.5]^3$ 内部。如果在框内，则采样贴花 Texture 的 Albedo、Normal、Roughness，按 Alpha 混合更新到 G-Buffer。

---

## 4. 实施计划优先级

- [ ] **Phase 1**：实现 `Vulkan_SSAO` 模块（最基础且效果最明显的阴影增强）。
- [ ] **Phase 2**：实现 `Vulkan_Deferred_Decals` 模块（充分发挥 G-Buffer 特性，实现动态细节覆盖）。
- [ ] **Phase 3**：实现 `Vulkan_SSR` 模块（屏幕空间光线步进反射）。

---

## 现有项目优化待办

现有 Demo 的代码审查结论、Bug 与重构项见 **[CODE_REVIEW_TODO.md](CODE_REVIEW_TODO.md)**（与上表新 Demo 规划分开维护）。
