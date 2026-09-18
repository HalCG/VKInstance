#version 450

// Pass 1 / Pass 2 共用：MVP 变换，输出世界空间位置/法线/UV

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;

layout(binding = 0) uniform VertexUbo {
    mat4 view;
    mat4 proj;
} vubo;

layout(push_constant) uniform PushConstants {
    mat4 model;
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
