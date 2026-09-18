# Vulkan 渲染性能调优与卡顿排查诊断指南

## 1. 概述与卡顿根因分析

在 Vulkan 渲染引擎开发中，交互卡顿、窗口拖拽冻结通常由 GPU 挂起（GPU Stalls）、频繁的资源重建或主线程阻塞引起。在本项目排查中，发现了以下三个核心瓶颈：

### 1.1 `VK_SUBOPTIMAL_KHR` 误触发频繁交换链重构（最主要卡顿源）
* **现象描述**：在 Windows 操作系统（尤其是开启 DPI 缩放、多显示器或特定显卡驱动）下，`vkQueuePresentKHR` 会高频或持续返回 `VK_SUBOPTIMAL_KHR`。
* **致命后果**：原代码在 `if (presentResult == VK_SUBOPTIMAL_KHR)` 时调用了 `recreateSwapchain()`。由于交换链重构内部包含 `vkDeviceWaitIdle()`、销毁与重建 Depth Image、ImageView 以及所有 Framebuffer，导致**渲染循环每秒被强制挂起与重建 60+ 次**，引发极严重的卡顿。
* **正确处理规范**：`VK_SUBOPTIMAL_KHR` 仅表示 Surface 尺寸与交换链不是绝对最优，但依然可以正常 Present。**绝不能在 `VK_SUBOPTIMAL_KHR` 时强制重建交换链**，仅应在 `VK_ERROR_OUT_OF_DATE_KHR` 或 GLFW `framebufferSizeCallback` 明确通知窗口尺寸改变时重建。

### 1.2 Windows 拖拽模态循环阻塞 (Win32 Modal Drag Freeze)
* **现象描述**：在 GLFW 窗口中拖拽标题栏或边框时，Windows 会进入 `WM_ENTERSIZEMOVE` 内部模态消息循环，主线程的 `while (!glfwWindowShouldClose)` 循环被挂起，画面瞬间冻结。
* **解决方案**：注册 `glfwSetWindowRefreshCallback` 回调函数。在 Windows 触发窗口重绘消息时，回调函数内部直接调用 `renderFrame()`，实现拖拽过程中的平滑持续绘制。

### 1.3 RenderPass 与 Pipeline 生命周期耦合
* **现象描述**：如果在窗口 Resize 时随交换链一同销毁了 `VkRenderPass` 句柄，会导致基于该 RenderPass 创建的 `VkPipeline` 句柄变为无属性引用，引发驱动异常或渲染阶段耗时激增。
* **解决方案**：将持久性 `VkRenderPass`（如 Offscreen Pass / MSAA Pass）的创建移至初始化阶段，Resize 时仅重建关联的 Attachment Image / View / Framebuffer，保证 Pipeline 句柄在整个应用生命周期内始终有效。

---

## 2. 关键代码优化实现

### 2.1 交换链重构判定修复
```cpp
// DemoApp.cpp / RenderingPathsApp.cpp
VkResult presentResult = vkQueuePresentKHR(ctx_.presentQueue(), &presentInfo);

// 修复后：去除了 VK_SUBOPTIMAL_KHR 触发条件
if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || resized_) {
    std::cout << "[Swapchain Log] Recreating swapchain due to "
              << (presentResult == VK_ERROR_OUT_OF_DATE_KHR ? "VK_ERROR_OUT_OF_DATE_KHR" : "Window Resize Event")
              << std::endl;
    recreateSwapchain();
}
```

### 2.2 窗口刷新回调（解决拖拽冻结）
```cpp
// 注册回调
glfwSetWindowRefreshCallback(ctx_.window(), windowRefreshCallback);

// 回调实现
void RenderingPathsApp::windowRefreshCallback(GLFWwindow * /*window*/) {
    if (s_instance_ && !s_instance_->framebufferResized_) {
        s_instance_->renderFrame();
    }
}
```

### 2.3 实时性能与诊断日志系统
在渲染主循环中加入高频统计与分类日志输出：
```cpp
// 每秒汇总输出性能指标
if (now - lastPerfLogTime >= 1.0) {
    const FrameStats stats = perf_.latest();
    std::cout << "[Perf Log] Path: " << pathName(currentPath_)
              << " | FPS: " << static_cast<int>(fps + 0.5)
              << " | Frame Time: " << stats.totalFrameMs << " ms"
              << " | Geom: " << stats.geometryPassMs << " ms"
              << " | Light: " << stats.lightingPassMs << " ms"
              << std::endl;
}
```

---

## 3. TAA (Temporal Anti-Aliasing) 原理与 Vulkan 适配性解析

### 3.1 Vulkan 适合做 TAA 吗？
**非常适合，且 Vulkan 是实现 TAA 的理想 API。**
现代商业级 Vulkan 引擎（如 Doom Eternal、Cyberpunk 2077、Unreal Engine Vulkan Backend、Godot 4）均使用 TAA / TSR 作为标准抗锯齿方案。Vulkan 具备以下天生优势：
1. **强大的 Compute Shader 交互**：TAA 的历史帧 Color Clamping 与 Sharpening 后处理可通过 Compute Shader 极高效完成。
2. **灵活的 Descriptor Indexing 与 Ping-pong 纹理绑定**：Vulkan 能高效管理 History Buffer（历史帧颜色与深度）与 Current Frame 的轮换绑定。

### 3.2 为什么基础 AA Demo 优先提供 MSAA 与 FXAA？
* **MSAA (Multisample Anti-Aliasing)**：硬件级多采样，直接在 Vulkan `VkRenderPass` / `VkPipelineMultisampleStateCreateInfo` 中配置，展示 Vulkan 原生硬件抗锯齿能力。
* **FXAA (Fast Approximate Anti-Aliasing)**：单帧屏幕空间后处理，只需一个 Fragment Shader 处理颜色边缘，适合作为基础屏幕空间抗锯齿对比。
* **TAA 的复杂性**：TAA 属于**时域（Multi-Frame Temporal）**技术，必须依赖一套完整的引擎数据管线支撑：
  1. **相机子像素抖动 (Subpixel Camera Jitter)**：每一帧在 Perspective 投影矩阵中施加微小的 Halton 序列偏移。
  2. **运动矢量图 (Motion Vector / Velocity Buffer)**：记录当前帧像素相对于上一帧的世界坐标/屏幕坐标位移（用于动态物体与相机移动时的历史像素对齐）。
  3. **历史帧 Ping-Pong Buffer 混合**：维护历史 Color / Depth 缓存并在当前帧进行 Exponential Moving Average (EMA) 混合。
  4. **历史污染与拖影消除 (Neighborhood Color Clamping / YCoCg Clipping)**：对当前像素周围 3x3 邻域在 YCoCg 空间构建 Bounding Box，截断超出的历史颜色，防止物体移动时产生鬼影 (Ghosting)。

---

## 4. 总结与最佳实践 checklist

- [x] **检查Present返回值**：严禁在 `VK_SUBOPTIMAL_KHR` 时盲目重建 Swapchain。
- [x] **窗口拖拽响应**：绑定 `glfwSetWindowRefreshCallback` 确保 Win32 消息循环期间持续渲染。
- [x] **句柄生命周期解耦**：保证 `VkPipeline` 引用的 `VkRenderPass` 在 Resize 期间不被提前销毁。
- [x] **分类诊断日志**：区分 `[Perf Log]`、`[Swapchain Log]`、`[Input Log]`，便于第一时间定位帧率跌落与重构耗时。
