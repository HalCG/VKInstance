# Vulkan OIT Stochastic Transparency（顺序无关透明 · 随机透明度）

本项目基于 Vulkan API 实现 **Stochastic Transparency（随机透明度）** 顺序无关透明（OIT）方案。场景与 OpenGL 参考工程 `01_GLInstance/OpenGL_OIT_Stochastic_Transparency` 对齐：**Spot 小牛** + 三块 **半透明彩色窗格**，单 Pass 渲染，Fragment Shader 写入 `gl_SampleMask`，硬件 MSAA Resolve 完成混合。

---

## 1. 项目特点

| 特点 | 说明 |
| :--- | :--- |
| **单 Pass 渲染** | 全场景一次 RenderPass 完成，无 Peel / Linked List / Resolve 后处理 |
| **与 OpenGL 场景一致** | 4 物体位置、相机、FOV、背景色与 OpenGL 版一一对应 |
| **MSAA + Sample Mask** | 多采样 RenderPass + 片元 `gl_SampleMask[0]`，等价 OpenGL `GL_SAMPLE_MASK` |
| **深度正常写入** | 透明/不透明均开启深度测试与深度写入，子采样级 Z-Test 处理遮挡 |
| **Assimp 真实模型** | `spot.obj` + `quad.obj`，纹理缺失时程序化窗格回退 |
| **共享 Demo 框架** | `Vulkan_DemoCommon`（DemoApp / DemoRhi / MSAA RenderPass） |
| **教学向注释** | 源码与 Shader 含 OpenGL↔Vulkan 对照说明 |

---

## 2. 关键技术原理

### 2.1 为什么需要 OIT？

传统 **Alpha Blending** 要求透明物体 **从远到近** 绘制；顺序错误时重叠区域颜色错误。  
**顺序无关透明** 在任意 draw 顺序下得到近似正确的叠加结果。

Stochastic Transparency 的核心思路：

> **用概率替代排序，用 MSAA 替代精确混合。**

### 2.2 算法直觉

将纹理 Alpha 解释为 **子采样覆盖概率**（coverage）：

- 设 Alpha = 0.6，MSAA 16x → 约 60% 的子采样被当前片元「命中」
- 每个子采样独立掷骰子：`if random(i, frameID) < coverage → bit i = 1`
- 写入 `gl_SampleMask[0]`，仅 mask 为 1 的子采样允许写入颜色/深度
- RenderPass **Resolve** 阶段硬件对 N 个子采样求平均 → 近似半透明

**无需排序的原因**：每个 MSAA 子采样拥有 **独立深度缓冲**。近处与远处片元若选中同一子采样，GPU 硬件 Z-Test 自动让近者覆盖远者；未选中的子采样留给后续物体。

### 2.3 单帧渲染流程

```
Step 1  查询 GPU 最高 MSAA 档位（优先 16x → 8x → 4x → …）
Step 2  创建 MSAA RenderPass
        attachment[0] 多采样颜色 (CLEAR)
        attachment[1] Resolve → Swapchain (STORE)
        attachment[2] 多采样深度 (CLEAR)
Step 3  vkCmdBeginRenderPass（3 个 ClearValue）
Step 4  逐物体 Draw（同一 Pass，无排序）
        Push: model + frameID + sampleCnt
        Fragment: texture.a → 随机 mask → gl_SampleMask[0]
Step 5  vkCmdEndRenderPass → 硬件 Resolve 到屏幕
```

### 2.4 Fragment Shader 核心逻辑

```glsl
float coverage = texture(texture_diffuse, vTexCoord).a;
uint randMask = 0u;
for (int i = 0; i < push.sampleCnt; ++i) {
    float r = rand(vec2(float(i), float(push.frameID)));
    if (r < coverage)
        randMask |= (1u << i);
}
gl_SampleMask[0] = int(randMask);
outColor = color;
```

- `frameID`：每物体递增 `% 4`，区分随机种子，避免样本竞争模式相同
- `sampleCnt`：与 MSAA 采样数一致（通常 4 / 8 / 16）

### 2.5 OpenGL ↔ Vulkan 状态映射

| OpenGL | Vulkan 本工程 |
| :--- | :--- |
| `glfwWindowHint(GLFW_SAMPLES, 16)` | `getStochasticSampleCount()` 取 GPU 最高档位 |
| `glEnable(GL_MULTISAMPLE)` | MSAA RenderPass `samples = N` |
| `glEnable(GL_SAMPLE_MASK)` | 多采样 Pipeline + 片元写 `gl_SampleMask` |
| `glEnable(GL_DEPTH_TEST)` | `PipelineInfo.depthTest = true` |
| `glDepthMask(GL_TRUE)` | DemoRhi 默认 `depthWriteEnable = true` |
| `quad.vert / quad.frag` | `shaders/stochastic.vert / .frag` |
| 默认帧缓冲 MSAA Resolve | `DemoRhi::createMsaaRenderPass` + `pResolveAttachments` |

### 2.6 MSAA 不可用时的回退

若 GPU 仅支持 1x 采样（`VK_SAMPLE_COUNT_1_BIT`），Fragment Shader 回退为 **discard 随机剔除**：

```glsl
if (random >= coverage) discard;
```

可运行但噪声更大，无法利用子采样级深度裁决。

### 2.7 Vulkan 实现要点

| 模块 | 实现 |
| :--- | :--- |
| **MSAA 离屏** | 多采样颜色 + 多采样深度 + 每 Swapchain 图像一个 FB |
| **ClearValue** | MSAA Pass 需 **3 个** ClearValue（attachment 0 颜色 + 2 深度；索引 1 为 Resolve，占位） |
| **Descriptor** | binding0=VertexUbo(view/proj)，binding1=diffuse 纹理 |
| **Push Constants** | model + frameID + sampleCnt（顶点/片段共用） |
| **无 Alpha Blend** | 不做传统混合；透明度由 MSAA Resolve 统计平均 |
| **相机** | `OrbitCamera`：半径 2、FOV 90°、Y 轴 Vulkan 翻转 |

---

## 3. 性能特征

| 维度 | 评估 |
| :--- | :--- |
| **Draw Call** | **极低**。单 Pass × 4 物体，无多层 Peel、无全屏 Resolve |
| **Fill-Rate** | **中**。MSAA N× 颜色/深度带宽；N=16 时约为单采样 16 倍子采样写入 |
| **显存** | **中**。1× 多采样颜色 + 1× 多采样深度（TRANSIENT）+ Swapchain |
| **CPU** | **极低**。每帧 UBO memcpy + 4 次 Push Constants，无 Atomic / 链表维护 |
| **GPU 着色** | 每像素 1 次片元 + `sampleCnt` 次随机循环（通常 ≤16） |
| **相对深度剥离** | 无 N 层全场景重绘，Fill-Rate 显著更低 |
| **相对链表 OIT** | 无 SSBO Atomic 竞争，实现更简单 |
| **画质代价** | 存在 **静态随机噪声**（frameID 固定）；低 Alpha 时颗粒感明显 |

**经验结论**：Stochastic Transparency 是三种 OIT 中 **CPU/GPU 逻辑开销最低** 的方案，适合实时预览与大量重叠透明，但 **画质为近似值**，不适合需要像素级精确混合的离线渲染。

---

## 4. 适用 / 不适用场景

### 适用

- **实时游戏 / 引擎预览**：大量半透明面片、玻璃、UI 叠加，可接受轻微噪声
- **重叠层数极多**：毛发、烟雾、粒子等 Depth Peeling 层数不够、链表 OIT 过重的场景
- **单 Pass 管线约束**：移动端或 VR 等 Fill-Rate 可换、但 Pass 数受限的平台
- **教学演示**：理解「概率 ≈ Alpha、MSAA ≈ 混合」的 OIT 思路
- **与 TAA 结合**：每帧变化 frameID + 时域累积可显著降噪（本 Demo 未实现 TAA）

### 不适用

- **离线 / 影视级渲染**：需要精确 Alpha 混合、无噪声
- **极低 Alpha 大面积覆盖**：如薄雾、全屏渐变透明（样本数不足时闪烁）
- **Deferred 管线直接复用**：需额外 MSAA G-Buffer 或 Forward Transparent Pass
- **无 MSAA 的老旧 GPU**：回退 discard 路径画质差
- **静态 frameID 的产品截图**：固定噪声图案可见（可改为每帧随机 frameID 改善）

---

## 5. 与其他 OIT 方案对比（本仓库）

| 方案 | 子项目 | Pass 数 | 精度 | 性能 | 噪声 |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **深度剥离** | `Vulkan_OIT_Depth_Peeling` | N 层 × 场景 + Blend | 精确 | Fill-Rate 高 | 无 |
| **链表 OIT** | `Vulkan_OIT_Linked_list` | 构建 + Resolve | 精确 | Atomic 开销 | 无 |
| **随机透明** | **本项目** | **1** | **近似** | **最低** | **有** |

| 特性 | Linked List | Depth Peeling | Stochastic（本项目） |
| :--- | :--- | :--- | :--- |
| 排序 | GPU 链表 | 逐层剥离 | 不排序（子采样 Z-Test） |
| 存储 | SSBO + Image | 多 FBO 深度 | 仅 MSAA 缓冲 |
| 实现复杂度 | 高 | 中 | **极低** |
| MSAA 兼容 | 困难 | 困难 | **原生依赖** |

---

## 6. 场景说明

与 OpenGL `StochasticTransparencyApp::renderScene` 一致：

| 顺序 | 模型 | 位置 | 纹理 | 说明 |
| :---: | :--- | :--- | :--- | :--- |
| 1 | spot.obj | `(0, 0, 0)` | spot.png | 不透明小牛 |
| 2 | quad.obj | `(0.3, -0.1, -0.8)` | window-b.png | 蓝色透明窗 |
| 3 | quad.obj | `(0.6, 0.6, -0.6)` | window-g.png | 绿色透明窗 |
| 4 | quad.obj | `(0, 0, 0)` | window-r.png | 红色透明窗 |

缩放均为 `0.5`；红窗与 spot 同位置，用于演示 **无序绘制 + 子采样深度裁决**。

---

## 7. 目录结构

```
Vulkan_OIT_Stochastic_Transparency/
├── CMakeLists.txt
├── README.md
├── include/
│   ├── AppConfig.hpp           # 窗口、相机、模型缩放
│   ├── StochasticDemo.hpp      # MSAA 渲染主类
│   ├── OitScene.hpp            # spot + 3× quad 场景
│   └── OrbitCamera.hpp         # 轨道相机
├── shaders/
│   ├── stochastic.vert         # MVP + uv
│   └── stochastic.frag         # gl_SampleMask 核心
├── resources/models/
│   ├── spot/                   # spot.obj
│   └── quad/                   # quad.obj, window-r/g/b.png（可选）
└── src/
    ├── main.cpp
    ├── StochasticDemo.cpp      # MSAA Pass 录制
    └── OitScene.cpp            # Assimp + stb_image
```

---

## 8. 编译与运行

在 `11_VkInstance` 根目录：

```bash
cmake --build out/build/x64-msvc-2026 --config Debug --target Vulkan_OIT_Stochastic_Transparency
```

运行（工作目录 = exe 目录）：

```text
out/build/x64-msvc-2026/Vulkan_OIT_Stochastic_Transparency/Debug/Vulkan_OIT_Stochastic_Transparency.exe
```

启动时会打印实际 MSAA 采样数，例如：

```text
Stochastic Transparency MSAA samples: 4
```

### 贴图资源（可选）

若目录中无 PNG，程序自动回退程序化窗格纹理：

```
resources/models/spot/spot.png
resources/models/quad/window-r.png
resources/models/quad/window-g.png
resources/models/quad/window-b.png
```

---

## 9. 操作说明

| 输入 | 功能 |
| :--- | :--- |
| **鼠标左键拖拽** | 绕场景旋转 |
| **滚轮** | 缩放视距 |
| **`←` / `→`** | 键盘旋转（与 OpenGL 版一致） |
| **ESC** | 退出 |

---

## 10. 延伸阅读

- OpenGL 参考实现：`01_GLInstance/OpenGL_OIT_Stochastic_Transparency`
- OpenGL 详细原理文档：`01_GLInstance/OpenGL_OIT_Stochastic_Transparency/docs/README.md`
- 同仓库深度剥离对比：`Vulkan_OIT_Depth_Peeling/README.md`
- MSAA RenderPass 实现：`Vulkan_DemoCommon/src/DemoRhi.cpp` → `createMsaaRenderPass`
- 论文：McGuire & Bavoil, *Stochastic Transparency*, HPG 2013

---

## 11. 已知限制与改进方向

| 项目 | 现状 | 可改进 |
| :--- | :--- | :--- |
| frameID | 每物体固定种子，噪声静态 | 每帧随机 / 结合 TAA 时域滤波 |
| MSAA 档位 | 取 GPU 最高可用（常见 4x/8x） | 强制 16x 或 UI 切换采样数 |
| 深度比较 | DemoRhi 默认 `LESS` | 改为 `LEQUAL` 与 OpenGL 完全一致 |
| 光照 | 仅纹理颜色，无 Blinn-Phong | 与 Depth Peeling 版对齐光照（非 OIT 必需） |
