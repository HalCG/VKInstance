# Vulkan OIT Depth Peeling（顺序无关透明 · 深度剥离）

本项目基于 Vulkan API 实现 **Depth Peeling（深度剥离）** 顺序无关透明（OIT）方案，场景与着色逻辑对齐同级 OpenGL 工程 `01_GLInstance/OpenGL_OIT_Depth_Peeling`：实体 **Spot 小牛** + 三块 **半透明彩色窗格**，支持鼠标轨道相机交互。

---

## 1. 项目特点

| 特点 | 说明 |
| :--- | :--- |
| **与 OpenGL 一一对应** | 三阶段 `initPeelBuffers` → `peelAndBlend` → `compositeToScreen`，混合方程相同 |
| **离屏多 Pass 架构** | Peel / Blend / Composite 分离；不直接在 Swapchain 上剥离 |
| **乒乓深度缓冲** | 两张深度纹理交替读写，记录“已剥离层”的最前深度 |
| **Assimp 真实模型** | `spot.obj` + `quad.obj`，加载方式与 `Vulkan_Rendering_Paths` 一致 |
| **共享 Demo 框架** | `Vulkan_DemoCommon`（DemoApp / DemoRhi / 全屏三角） |
| **教学向注释** | 头文件与 `render()` 内标注 Phase 0/1/2 及 Barrier 时机 |

---

## 2. 关键技术原理

### 2.1 为什么需要 OIT？

传统 **Alpha Blending** 要求按 **从远到近** 绘制透明物体；若绘制顺序错误，重叠区域颜色错误。  
**顺序无关透明** 在任意 draw 顺序下都能得到正确叠加结果。深度剥离是经典 OIT 方案之一。

### 2.2 深度剥离核心思想

每一 **Peel Pass** 只保留“比已记录深度更远”的片元（即下一层透明表面）：

1. 片元着色器读取 **上一层深度纹理** `frontDepth`
2. 若 `gl_FragCoord.z <= frontDepth` → `discard`（该像素已被更近层占用）
3. 剩余片元经硬件 **深度测试 LESS**，写入本层 **outputDepth**（本层最近深度）
4. 本层颜色写入 `peelColor_`

循环多次即可逐层“剥开”同一像素上的重叠透明片元。

### 2.3 三阶段渲染流程（每帧）

```
Phase 0  initPeelBuffers
  accumColor_  ← clear (0, 0, 0, 1)     // A_dst=1，透光率初始为 100%
  depthPing_[0/1] ← clear depth 0.0      // 第一层 z>0 的片元才能通过

Phase 1  peelAndBlend（最多 kMaxDepthPeelLayers = 10 层）
  循环每层:
    A. Peel Pass   → peelColor_ + depthPing_[output]
    B. Blend Pass  → 将 peelColor_ 以 Front-to-Back 公式并入 accumColor_
    C. swap        inputDepthIndex_ ↔ outputDepthIndex_

Phase 2  compositeToScreen
  out = accum.rgb + background * accum.a   // 未覆盖区域露出背景色
```

### 2.4 Front-to-Back 混合（`oitFrontToBackBlend`）

Blend Pass 使用 `DemoRhi::PipelineInfo::oitFrontToBackBlend = true`，等价 OpenGL：

```cpp
glBlendFuncSeparate(GL_DST_ALPHA, GL_ONE, GL_ZERO, GL_ONE_MINUS_SRC_ALPHA);
```

| 通道 | 公式 | 含义 |
| :--- | :--- | :--- |
| **RGB** | `C_dst = C_src * A_dst + C_dst` | 新层颜色按 **剩余透光率** `A_dst` 加权叠入 |
| **Alpha** | `A_dst = A_dst * (1 - A_src)` | 更新剩余透光率 |

- 帧初 `accum = (0,0,0,1)` → `A_dst = 1`
- 实体小牛 `A_src ≈ 1` → 透光率变为 0，**后续层自动被遮挡**（无需排序）
- 半透明窗格 `A_src < 1` → 多层可共存

详见 `Vulkan_DemoCommon/include/DemoRhi.hpp` 中 `oitFrontToBackBlend` 字段注释。

### 2.5 Vulkan 实现要点

| 模块 | 实现 |
| :--- | :--- |
| **累积缓冲** | `RGBA16F` 离屏 `accumColor_`，避免多层混合精度损失 |
| **Peel RenderPass** | 颜色+深度 `CLEAR`，`finalLayout` 供采样 |
| **Blend RenderPass** | `LOAD` 保留 accum，叠加本层 |
| **深度采样** | **NEAREST** Sampler，避免深度插值导致剥离错误 |
| **深度历史初值** | `0.0`（非默认 1.0），保证第一层正常剥离 |
| **model 矩阵** | Push Constant，每物体一次 draw |
| **同步** | Peel → Blend、Blend → Composite 间 `vkCmdPipelineBarrier` |

### 2.6 与 OpenGL 参考的映射

| OpenGL | Vulkan 本工程 |
| :--- | :--- |
| `fboAccum_.color` | `accumColor_` |
| `fboPeel_.color` | `peelColor_` |
| `depthTexture(0/1)` | `depthPing_[0/1]` |
| `glBlendFuncSeparate(...)` | `oitFrontToBackBlend` |
| `depth_peeling_render.frag` | `shaders/depth_peel.frag` |
| `depth_peeling_final.frag` | `shaders/composite.frag` |

---

## 3. 性能特征

| 维度 | 评估 |
| :--- | :--- |
| **Fill-Rate** | **高**。每层 Peel 全屏重绘整个场景（本 Demo 4 个物体 × 最多 10 层 ≈ 40 次几何 Pass + 10 次全屏 Blend） |
| **显存** | 中。2× 全屏 `RGBA16F` + 2× 深度 + Peel 中间色 |
| **带宽** | Peel/Blend 交替读写离屏附件，移动端压力较大 |
| **CPU** | 低。无每帧 Descriptor 重建；仅 UBO memcpy + 每层更新 depth 绑定 |
| **Early-Out** | OpenGL 版有 `GL_SAMPLES_PASSED` 遮挡查询提前退出；**本 Vulkan 版固定跑满 10 层**（可优化项） |
| **相对链表 OIT** | 剥离层数有上限，超薄透明层过多时可能欠剥离；但实现简单、无 Atomic 竞争 |
| **相对随机透明** | 无噪声、结果稳定 deterministic，适合教学与品质要求稳定的场景 |

**经验结论**：深度剥离适合 **透明层数少且可控**（UI 玻璃、少量重叠窗格、角色+特效），不适合海量粒子或数百层重叠的毛发/烟雾。

---

## 4. 适用 / 不适用场景

### 适用

- 教学演示 OIT 原理（与 OpenGL 版对照阅读）
- 透明物体 **数量有限**、**重叠层数 ≤ 10** 的关卡场景
- 需要 **稳定无噪声** 的透明叠加（相对 Stochastic Transparency）
- 不想引入 **Atomic / Linked List** 复杂度的项目原型

### 不适用

- 大量粒子、雨雪、毛发等 **极高重叠层数**
- 移动端 **Fill-Rate 紧张** 且透明占屏比大
- 需要 **MSAA + 透明** 同时高质量（Peel 与 MSAA 结合成本高）
- 延迟渲染管线中直接复用 G-Buffer 深度（需额外 Peel 缓冲与专用 Pass）

---

## 5. 与其他 OIT 方案对比（本仓库）

| 方案 | 子项目 | 优点 | 缺点 |
| :--- | :--- | :--- | :--- |
| **深度剥离** | 本项目 | 直观、无 Atomic、画质稳定 | Fill-Rate 高、层数上限 |
| **链表 OIT** | `Vulkan_OIT_Linked_list` | 层数理论无限、单 Pass 构建 | Atomic 竞争、显存/Resolve 复杂 |
| **随机透明** | `Vulkan_OIT_Stochastic_Transparency` | 极低开销 | 噪声、时域不稳定 |

---

## 6. 目录结构

```
Vulkan_OIT_Depth_Peeling/
├── CMakeLists.txt
├── README.md
├── include/
│   ├── AppConfig.hpp           # 窗口、相机、剥离层数等常量
│   ├── DepthPeelingDemo.hpp    # 三阶段流程与离屏资源（主文档）
│   ├── OitScene.hpp            # spot + 3× quad 场景
│   └── OrbitCamera.hpp         # 轨道相机 + Vulkan Y 翻转
├── shaders/
│   ├── depth_peel.vert / .frag # Peel：discard + Blinn-Phong + 纹理 Alpha
│   ├── blend.vert / .frag      # Blend：全屏采样 peelColor_
│   └── composite.frag          # Composite：accum + 背景
├── resources/models/
│   ├── spot/                   # spot.obj, spot.mtl（贴图 spot.png 需自行放置）
│   └── quad/                   # quad.obj, window-r/g/b.png（可选）
└── src/
    ├── main.cpp
    ├── DepthPeelingDemo.cpp    # Peel/Blend/Composite 命令录制
    └── OitScene.cpp            # Assimp + stb_image 加载
```

---

## 7. 编译与运行

```bash
cmake --build <Build-Dir> --config Debug --target Vulkan_OIT_Depth_Peeling
```

运行（工作目录 = exe 目录）：

```text
out/build/x64-msvc-2026/Vulkan_OIT_Depth_Peeling/Debug/Vulkan_OIT_Depth_Peeling.exe
```

### 贴图资源（可选）

若目录中无 PNG，程序自动回退：小牛 → 1×1 白纹理；窗格 → 程序化半透明色块。

```
resources/models/spot/spot.png
resources/models/quad/window-r.png
resources/models/quad/window-g.png
resources/models/quad/window-b.png
```

### 操作

| 输入 | 功能 |
| :--- | :--- |
| **鼠标左键拖拽** | 绕场景旋转 |
| **滚轮** | 缩放视距 |
| **`←` / `→`** | 键盘旋转 |
| **ESC** | 退出（DemoApp 默认） |

---

## 8. 延伸阅读

- OpenGL 参考实现：`01_GLInstance/OpenGL_OIT_Depth_Peeling`
- OpenGL 工程内 `OIT_MD/` 目录有 `peelAndBlend`、`glBlendFuncSeparate` 等专题笔记
- 混合配置源码：`Vulkan_DemoCommon/include/DemoRhi.hpp` → `oitFrontToBackBlend`
