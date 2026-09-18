// geometry.frag — Deferred Pass1：写入 G-Buffer
//
// MRT 输出 (对应 G-Buffer RenderPass 的 3 个 color attachment):
//   location 0 → gAlbedo   (RGBA8, 漫反射贴图颜色)
//   location 1 → gNormal   (RGBA16F, 世界空间法线)
//   location 2 → gPosition (RGBA16F, 世界空间坐标 xyz)
//
// 深度由 RenderPass 的 depth attachment 自动写入，Pass2 用 depth 判断天空像素

#version 450

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vTexCoords;

layout(set = 0, binding = 1) uniform sampler2D texture_diffuse1;

layout(location = 0) out vec4 gAlbedo;
layout(location = 1) out vec4 gNormal;
layout(location = 2) out vec4 gPosition;

void main() {
    gAlbedo = texture(texture_diffuse1, vTexCoords);
    gNormal = vec4(normalize(vNormal), 1.0);
    gPosition = vec4(vWorldPos, 1.0);
}
