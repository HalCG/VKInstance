// mesh.vert — 三种渲染路径共用的顶点着色器
//
// 顶点布局 (binding 0, stride 32):
//   location 0: vec3 position
//   location 1: vec3 normal
//   location 2: vec2 uv
//
// 资源:
//   set0 binding0 UBO: view + proj（每帧由 Scene::writeCameraUbo 更新）
//   push constant:    model 矩阵（每个 draw call 由 vkCmdPushConstants 传入）
//
// 输出给片元着色器:
//   vWorldPos  — 世界空间位置（光照计算用）
//   vNormal    — 世界空间法线
//   vTexCoords — 纹理坐标

#version 450

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoords;

layout(set = 0, binding = 0) uniform MeshUbo {
    mat4 view;
    mat4 proj;
} ubo;

layout(push_constant) uniform PushConstants {
    mat4 model;
} push;

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec2 vTexCoords;

void main() {
    vWorldPos = vec3(push.model * vec4(aPos, 1.0));
    vNormal = mat3(transpose(inverse(push.model))) * aNormal;
    vTexCoords = aTexCoords;
    gl_Position = ubo.proj * ubo.view * vec4(vWorldPos, 1.0);
}
