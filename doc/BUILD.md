# VkInstance 构建与运行

## 你要运行的程序在哪？

**固定规则：按配置分目录 `run/Debug/<项目名>/` 与 `run/Release/<项目名>/`**

```text
11_VkInstance/
└── run/
    ├── Debug/
    │   ├── Vulkan_OIT_Depth_Peeling/
    │   │   ├── Vulkan_OIT_Depth_Peeling.exe
    │   │   ├── assimp-vc143-mtd.dll
    │   │   └── resources/
    │   └── Vulkan_Rendering_Paths/
    │       └── ...
    └── Release/
        ├── Vulkan_OIT_Depth_Peeling/
        │   ├── Vulkan_OIT_Depth_Peeling.exe
        │   ├── assimp-vc143-mt.dll
        │   └── resources/
        └── ...
```

只需记住：**`run` → Debug 或 Release → 项目名 → exe**。Debug / Release 互不覆盖。

---

## 构建命令

在 `11_VkInstance/` 下：

```bash
cmake --preset x64-msvc-2026
cmake --build out/build/x64-msvc-2026 --config Debug
```

只编一个 Demo：

```bash
cmake --build out/build/x64-msvc-2026 --config Debug --target Vulkan_OIT_Depth_Peeling
```

构建成功后自动完成：

- exe 写入 `run/Debug/<Target>/` 或 `run/Release/<Target>/`（与 `--config` 一致）
- 运行时 DLL 拷贝到同目录（`vk_ws_deploy_module_dlls`）：
  - Debug：`assimp-vc143-mtd.dll`、`zlibd1.dll`、`minizip.dll`、`poly2tri.dll`、`pugixml.dll` 等
  - Release：`assimp-vc143-mt.dll`、`zlib1.dll` 等同上依赖的 Release 版
- `resources/shaders/*.spv` 与 `resources/models/` 部署到同目录

---

## 目录分工（两层结构）

| 目录 | 用途 | 是否需要手动打开 |
| :--- | :--- | :--- |
| **`run/`** | 可运行程序 + DLL + 资源 | **是，日常只用这个** |
| `out/build/<preset>/` | CMake 工程、.obj、.lib、SPIR-V 编译中间文件 | 否，仅排错构建时 |

> 不要使用根目录 `cmake -B build`；`build/` 为历史遗留，已忽略。

---

## Preset

| Preset | 说明 |
| :--- | :--- |
| `x64-msvc-2026` | Visual Studio 2026（推荐） |
| `x64-clang-debug` | Ninja + Clang Debug |

无论用哪个 Preset 编译，**运行路径始终是 `run/Debug/<Target>/` 或 `run/Release/<Target>/`**（与构建配置一致）。

---

## 子项目 CMake 模板

```cmake
add_executable(MyDemo ...)
vk_ws_set_run_output(MyDemo)          # → run/Debug|Release/MyDemo/

target_link_libraries(MyDemo PRIVATE ...)
vk_ws_link_assimp(MyDemo)

vk_ws_compile_shaders(MyDemo ...)
vk_ws_deploy_runtime(MyDemo)

include("${CMAKE_SOURCE_DIR}/cmake/VulkanWorkspaceResources.cmake")
vk_ws_deploy_static_resources(MyDemo)
```

---

## 独立子项目

`Vulkan_NaniteInUE5.5.4` 不在此约定内，见其目录 README。

---

## 相关文档

| 文档 | 内容 |
|------|------|
| [CODE_REVIEW_TODO.md](CODE_REVIEW_TODO.md) | 各子项目审查结论与优化待办（P0–P3） |
| [roadmap_and_todo.md](roadmap_and_todo.md) | 后续新 Demo 路线图（SSAO / SSR / Decals） |
