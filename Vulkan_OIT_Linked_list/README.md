# Vulkan OIT Linked List（顺序无关透明 · 逐像素链表）

本项目基于 Vulkan API 实现 **Per-Pixel Linked List OIT（PPLL）** 方案，场景与着色逻辑对齐同级 OpenGL 工程 `01_GLInstance/OpenGL_OIT_Linked_list`：实体 **Spot 小牛** + 三块 **半透明彩色窗格**，支持鼠标轨道相机交互。

---

## 1. 项目特点

| 特点 | 说明 |
| :--- | :--- |
| **与 OpenGL 三 Pass 一一对应** | `renderOpaquePass` → `renderTransparentPass` → `renderCompositePass` |
| **单 Pass 收集透明片元** | Pass 2 用 `atomicAdd` + `atomicExchange` 头插法写入 SSBO 链表 |
| **Pass 3 全屏 Resolve** | 采样 opaque 颜色 + 遍历链表 → Back-to-Front Over 混合 |
| **Assimp 真实模型** | `spot.obj` + `quad.obj`，与 `Vulkan_OIT_Depth_Peeling` 共用 `OitScene` |
| **共享 Demo 框架** | `Vulkan_DemoCommon`（DemoApp / DemoRhi / 全屏三角） |
| **教学向注释** | 头文件、`render()` 与 Shader 内标注 Pass / Barrier / Descriptor 绑定 |

---

## 2. 关键技术原理

### 2.1 为什么需要 OIT？

传统 Alpha Blending 要求透明物体 **从远到近** 绘制；重叠、穿插或视角变化时 CPU 排序代价高且易错。**顺序无关透明** 将排序交给 GPU，在像素级收集所有透明片元后再混合。

### 2.2 Linked List OIT 核心思想

每个屏幕像素维护一条 **单链表**，链表中每个节点存储 `{ color, depth, next }`：

1. **Pass 1**：绘制不透明物体，输出 `opaqueColor` + `opaqueDepth`
2. **Pass 2**：绘制透明物体；片元 Shader 中：
   - 用 opaque 深度剔除被遮挡片元
   - `atomicAdd(nodeCounter, 1)` 分配全局节点索引
   - `atomicExchange(heads[pixel], nodeIndex)` 头插法入链
   - 写入 `nodes[nodeIndex]`
3. **Pass 3**：全屏 Pass 遍历当前像素链表 → 去重 → 插入排序（远→近）→ Over 混合到 opaque 底色

### 2.3 三 Pass 渲染流程（每帧）

```
Pass 1  Opaque
  spot → Blinn-Phong → opaqueColor (RGBA16F) + opaqueDepth

Pass 2  OIT Build（reset head/counter → draw 3× quad）
  片元: texelFetch(opaqueDepth) 裁切
        atomicAdd + atomicExchange 头插 SSBO 链表
  管线: depthTest=OFF（Vulkan 不能同时 depth attachment + sampler）

Pass 3  Composite → Swapchain
  全屏三角: sample(opaqueColor) + 遍历 heads[]/nodes[]
            Back-to-Front Over 混合
  Barrier: Pass2 SSBO 写 → Pass3 SSBO 读
```

### 2.4 OIT 缓冲区（Vulkan 映射）

| OpenGL | Vulkan 本工程 | 作用 |
| :--- | :--- | :--- |
| `headPtrTexture` (Image R32UI) | `headBuffer_` (SSBO) | 每像素链表头索引，`0xFFFFFFFF` = 空 |
| `atomicBuffer` (Atomic Counter) | `nodeCounterBuffer_` (SSBO) | 全局节点分配计数器，每帧清零 |
| `linkedListBuffer` (SSBO) | `nodeDataBuffer_` (SSBO) | 节点池 `{ vec4 color; float depth; uint next; }` |
| `glMemoryBarrier` | `barrierOitForComposite()` | Pass 2 写完成后再读 |

节点容量：`maxNodes = width × height × kMaxFragmentsPerPixel`（默认 20 片元/像素）。

### 2.5 Vulkan 与 OpenGL 的实现差异

| 点 | OpenGL | Vulkan 本工程 |
| :--- | :--- | :--- |
| 头指针 | `uimage2D` + `imageAtomicExchange` | SSBO `heads[]` + `atomicExchange` |
| Pass 2 深度测试 | 共享 depth attachment + `glDepthMask(false)` | **无 depth attachment**；Shader `texelFetch(texture_depth)` 手动 discard |
| Pass 2 颜色输出 | 写入 oitRenderFBO | 片元末尾 `discard`（dummy color attachment 满足 FBO 完整性） |
| 同步 | `glMemoryBarrier` | `vkCmdPipelineBarrier`（buffer + image） |
| 相机 | 键盘/鼠标 | 左键拖拽 + 滚轮 + 方向键（**无自动旋转**） |

### 2.6 与 OpenGL 参考的 Shader 映射

| OpenGL | Vulkan 本工程 |
| :--- | :--- |
| `blinnPhong.frag` | `shaders/blinn_phong.frag` |
| `oitRender.frag` | `shaders/oit_build.frag` |
| `composite.frag` | `shaders/composite.frag` |

---

## 3. 性能特征

| 维度 | 评估 |
| :--- | :--- |
| **Fill-Rate** | **中**。Pass 1/2 各一次几何绘制；Pass 3 一次全屏 |
| **显存** | **高**。`maxNodes × 32B` 节点池 + 全屏 head buffer（800×600×20 ≈ 9.6M 节点 ≈ 300MB） |
| **Atomic 竞争** | 热点像素（大量重叠透明）上 `atomicAdd`/`atomicExchange` 可能成为瓶颈 |
| **CPU** | 低。每帧 UBO memcpy + 固定三 Pass |
| **相对 Depth Peeling** | 无多层几何 Pass，但 SSBO 显存与 Atomic 开销更大 |
| **相对 Stochastic** | 精确无噪声，但内存与同步更重 |

**经验结论**：链表 OIT 适合 **透明层数较多但可预估上限**、需要 **精确混合** 的场景；不适合显存紧张或海量透明片元的移动端。

---

## 4. 适用 / 不适用场景

### 适用

- 教学演示 PPLL 原理（配合 OpenGL `docs/README.md` 阅读）
- 透明重叠层数 **超过 Depth Peeling 固定层数** 的场景
- 需要 **单次几何 Pass 收集** 所有透明片元
- PC / 主机平台，显存充足

### 不适用

- 移动端 / 集成显卡（SSBO + Atomic 压力大）
- 粒子、雨雪等 **不可预估片元数量**（节点池易溢出）
- 需要 MSAA 高质量透明（PPLL 与 MSAA 结合复杂）
- 延迟渲染直接复用 G-Buffer（需独立 OIT Pass 与缓冲）

---

## 5. 与其他 OIT 方案对比（本仓库）

| 方案 | 子项目 | 优点 | 缺点 |
| :--- | :--- | :--- | :--- |
| **深度剥离** | `Vulkan_OIT_Depth_Peeling` | 直观、无 Atomic、画质稳定 | Fill-Rate 高、层数上限 |
| **链表 OIT** | **本项目** | 层数理论无限、单 Pass 构建 | Atomic 竞争、显存/Resolve 复杂 |
| **随机透明** | `Vulkan_OIT_Stochastic_Transparency` | 极低开销 | 噪声、时域不稳定 |

---

## 6. 目录结构

```
Vulkan_OIT_Linked_list/
├── CMakeLists.txt
├── README.md
├── include/
│   ├── AppConfig.hpp           # 窗口、相机、节点上限等常量
│   ├── LinkedListDemo.hpp      # 三 Pass 流程与 OIT 缓冲（主文档）
│   ├── OitScene.hpp            # spot + 3× quad 场景（与 DepthPeeling 共用）
│   └── OrbitCamera.hpp         # 轨道相机 + Vulkan Y 翻转
├── shaders/
│   ├── lit.vert                # Pass1/2 共用顶点变换
│   ├── blinn_phong.frag        # Pass1：不透明 Blinn-Phong
│   ├── oit_build.vert          # Pass2：同 lit.vert + 窗口尺寸 push
│   ├── oit_build.frag          # Pass2：深度裁切 + 链表头插法
│   ├── composite.vert          # Pass3：全屏三角
│   └── composite.frag          # Pass3：链表排序 + Over 混合
├── resources/models/
│   ├── spot/                   # spot.obj, spot.png
│   └── quad/                   # quad.obj, window-r/g/b.png
└── src/
    ├── main.cpp                # DemoApp 入口与输入回调
    ├── LinkedListDemo.cpp      # 三 Pass 命令录制
    └── OitScene.cpp            # Assimp + stb_image 加载
```

---

## 7. 编译与运行

### 7.1 编译

在 Vulkan 工作区 build 目录执行：

```powershell
cd I:\opengl\11_VkInstance\build
cmake --build . --target Vulkan_OIT_Linked_list --config Debug
```

### 7.2 可执行文件路径（重要）

**请从此目录运行**，CMake POST_BUILD 会把 DLL、模型、SPIR-V 部署到 exe 旁：

```text
I:\opengl\11_VkInstance\build\Vulkan_OIT_Linked_list\Debug\
├── Vulkan_OIT_Linked_list.exe    ← 运行这个
├── assimp-vc143-mt.dll
└── resources\
    ├── models\...
    └── shaders\*.spv
```

常见误启动路径：

| 路径 | 说明 |
| :--- | :--- |
| `Vulkan_OIT_Linked_list\`（源码目录） | **没有 exe** |
| `build\Vulkan_OIT_Depth_Peeling\Debug\` | 另一个项目 |
| `01_GLInstance\OpenGL_OIT_Linked_list\` | OpenGL 版本 |

### 7.3 启动方式

**PowerShell：**

```powershell
cd I:\opengl\11_VkInstance\build\Vulkan_OIT_Linked_list\Debug
.\Vulkan_OIT_Linked_list.exe
```

**Visual Studio：** 打开 `build\Vulkan_OIT_Linked_list\Vulkan_OIT_Linked_list.slnx`，设 **Vulkan_OIT_Linked_list** 为启动项目，选 Debug 后 F5。

### 7.4 控制台预期输出

```
[LinkedListOIT] Scene ready: 4 objects | spot indices=... | quad indices=...
```

若 `indexCount=0` 或出现 `[OitScene] Failed to load model`，检查 `resources/models` 是否在 exe 同目录。

### 7.5 贴图资源（可选）

若目录中无 PNG，程序自动回退：小牛 → 1×1 白纹理；窗格 → 程序化半透明色块。

---

## 8. 操作说明

| 输入 | 功能 |
| :--- | :--- |
| **鼠标左键拖拽** | 绕场景水平旋转 |
| **滚轮** | 缩放视距（1.0 ~ 8.0） |
| **`←` / `→`** | 键盘微调旋转 |
| **ESC** | 退出（DemoApp 默认） |

> 场景 **不会自动旋转**；初始轨道角 `227°`，与 OpenGL 版一致。

---

## 9. Descriptor 绑定速查

### Pass 1 / Pass 2 光照（lit / oit_build）

| Binding | 类型 | 内容 |
| :---: | :--- | :--- |
| 0 | UBO | view / proj |
| 1 | UBO | cameraPos, lightPos, k, maxNodes |
| 2 | Combined Image | diffuse 纹理 |
| 3 | Combined Image | opaque depth（仅 oit_build） |
| 4 | SSBO | `heads[]` 每像素头指针 |
| 5 | SSBO | `nodeCounter` |
| 6 | SSBO | `nodes[]` 节点池 |

### Pass 3 Composite

| Binding | 类型 | 内容 |
| :---: | :--- | :--- |
| 0 | Combined Image | opaque 颜色 |
| 1 | SSBO | `heads[]` |
| 2 | SSBO | `nodes[]` |

---

## 10. 延伸阅读

- OpenGL 参考实现：`01_GLInstance/OpenGL_OIT_Linked_list`
- OpenGL 详细原理文档：`01_GLInstance/OpenGL_OIT_Linked_list/docs/README.md`
- 同场景 Depth Peeling Vulkan 版：`Vulkan_OIT_Depth_Peeling`
- 场景加载参考：`Vulkan_OIT_Depth_Peeling` / `OpenGL_OIT_Depth_Peeling`
