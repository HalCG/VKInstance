#version 450

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoords;
layout(location = 3) in vec4 aColor;

layout(binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    vec4 cameraPos;
    vec4 lightDir;
} ubo;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 colorTint;
    int useTexture;
} push;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec2 vTexCoords;
layout(location = 2) out vec4 vColor;
layout(location = 3) out vec3 vWorldPos;

void main() {
    vec4 worldPos = push.model * vec4(aPos, 1.0);
    vWorldPos = worldPos.xyz;
    vNormal = mat3(transpose(inverse(push.model))) * aNormal;
    vTexCoords = aTexCoords;
    vColor = aColor * push.colorTint;
    gl_Position = ubo.proj * ubo.view * worldPos;
}
