// =============================================================================
// gbuffer_debug.frag — Deferred 模式下 G-Buffer 可视化调试
// =============================================================================
//
// 【触发】按 G 键（仅 Deferred 路径），RenderingPathsApp::showGBufferDebug_ = true
//
// 【绘制方式】
//   与 deferred_lighting 相同：deferred_lighting.vert 生成全屏三角形（3 顶点，无 VBO）
//   本 Shader 替换 deferred_lighting.frag，直接采样 G-Buffer 纹理输出到 Swapchain
//
// 【debugMode（set0 binding0 UBO）】
//   1 → 显示 albedo（RT0，RGBA8）
//   2 → 显示 normal（RT1，映射到 [0,1] 便于观察：rgb * 0.5 + 0.5）
//   其他 → 显示 world position（RT2；C++ 变量名 gMaterial_，geometry.frag 输出 gPosition）
//
// 【Descriptor 布局】
//   set0 binding0: DebugUbo { int debugMode; }
//   set0 binding1-3: uGAlbedo, uGNormal, uGMaterial（第三张实为 world position）
//
// =============================================================================

#version 450

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform DebugUbo {
    int debugMode;
} ubo;

layout(set = 0, binding = 1) uniform sampler2D uGAlbedo;
layout(set = 0, binding = 2) uniform sampler2D uGNormal;
layout(set = 0, binding = 3) uniform sampler2D uGMaterial;

void main() {
    if (ubo.debugMode == 1) {
        outColor = texture(uGAlbedo, vUV);
    } else if (ubo.debugMode == 2) {
        outColor = vec4(texture(uGNormal, vUV).rgb * 0.5 + 0.5, 1.0);
    } else {
        outColor = texture(uGMaterial, vUV);
    }
}
