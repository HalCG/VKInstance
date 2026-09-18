#version 450
/**
 * @file stochastic.vert
 * @brief Stochastic Transparency 顶点着色器
 *
 * 与 OpenGL quad.vert 对应：
 *   - 输入：pos / normal / uv（Assimp 加载）
 *   - UBO：view + proj（每帧更新）
 *   - Push Constants：model（每 draw 更新）
 *   - 输出：uv 供 Fragment Shader 采样 diffuse 纹理
 */

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;

layout(binding = 0) uniform VertexUbo {
    mat4 view;
    mat4 proj;
} vubo;

layout(push_constant) uniform PushConstants {
    mat4 model;
    int frameID;
    int sampleCnt;
} push;

layout(location = 0) out vec2 vTexCoord;

void main() {
    vTexCoord = aTexCoord;
    vec4 worldPos = push.model * vec4(aPos, 1.0);
    gl_Position = vubo.proj * vubo.view * worldPos;
}
