#version 450

// Pass 2 顶点：同 lit.vert，额外 push 窗口宽高供片元计算 pixelIndex

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;

layout(binding = 0) uniform VertexUbo {
    mat4 view;
    mat4 proj;
} vubo;

layout(push_constant) uniform PushConstants {
    mat4 model;
    uint width;
    uint height;
} push;

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec2 vTexCoord;

void main() {
    vTexCoord = aTexCoord;
    vWorldPos = vec3(push.model * vec4(aPos, 1.0));
    vNormal = mat3(transpose(inverse(push.model))) * aNormal;
    gl_Position = vubo.proj * vubo.view * vec4(vWorldPos, 1.0);
}
