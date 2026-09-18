#version 450

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec2 vTexCoords;
layout(location = 2) in vec4 vColor;
layout(location = 3) in vec3 vWorldPos;

layout(binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    vec4 cameraPos;
    vec4 lightDir;
} ubo;

layout(binding = 1) uniform sampler2D uTexture;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 colorTint;
    int useTexture;
} push;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 N = normalize(vNormal);
    vec3 L = normalize(ubo.lightDir.xyz);
    vec3 V = normalize(ubo.cameraPos.xyz - vWorldPos);
    vec3 H = normalize(L + V);

    float diff = max(dot(N, L), 0.0);
    float spec = pow(max(dot(N, H), 0.0), 32.0);
    float ambient = 0.22;

    vec4 baseColor = vColor;
    if (push.useTexture != 0) {
        baseColor *= texture(uTexture, vTexCoords);
    }

    vec3 lightColor = vec3(1.0, 0.96, 0.90);
    vec3 diffuse = baseColor.rgb * diff * lightColor;
    vec3 specular = vec3(0.4) * spec * lightColor;
    vec3 finalColor = baseColor.rgb * ambient + diffuse + specular;

    outColor = vec4(finalColor, baseColor.a);
}
