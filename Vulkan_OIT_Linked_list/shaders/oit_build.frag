#version 450

// Pass 2：透明片元收集 — 对应 OpenGL oitRender.frag
//
// 1. texelFetch(opaque depth) 剔除被不透明物体遮挡的片元
// 2. Blinn-Phong 计算带 alpha 的颜色
// 3. atomicAdd 分配节点索引；atomicExchange 头插法写入 SSBO 链表
// 4. discard — 不写 FBO 颜色（链表数据在 SSBO 中）

#define MAX_FRAGMENTS 75

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vTexCoord;

struct Node {
    vec4 color;
    float depth;
    uint next;
};

layout(std430, set = 0, binding = 4) buffer HeadPointerBuffer {
    uint heads[];
};

layout(std430, set = 0, binding = 5) buffer NodeCounter {
    uint nodeCounter;
};

layout(std430, set = 0, binding = 6) buffer NodeStorage {
    Node nodes[];
};

layout(binding = 1) uniform FragUbo {
    vec4 cameraPos;
    vec4 lightPos;
    vec4 k;
    uint maxNodes;
} fubo;

layout(binding = 2) uniform sampler2D texture_diffuse;
layout(binding = 3) uniform sampler2D texture_depth;

layout(push_constant) uniform PushConstants {
    mat4 model;
    uint width;
    uint height;
} push;

void main() {
    ivec2 pixelPos = ivec2(gl_FragCoord.xy);
    float opaqueDepth = texelFetch(texture_depth, pixelPos, 0).r;
    if (gl_FragCoord.z > opaqueDepth + 0.0001) {
        discard;
    }

    vec3 lightColor = vec3(1.0);
    float ambientStrength = fubo.k.x;
    vec3 ambient = ambientStrength * lightColor;

    float diffuseStrength = fubo.k.y;
    vec3 normalDir = normalize(vNormal);
    vec3 lightDir = normalize(fubo.lightPos.xyz - vWorldPos);
    vec3 diffuse = diffuseStrength * max(dot(normalDir, lightDir), 0.0) * lightColor;

    float specularStrength = fubo.k.z;
    vec3 viewDir = normalize(fubo.cameraPos.xyz - vWorldPos);
    vec3 halfwayDir = normalize(lightDir + viewDir);
    vec3 specular = specularStrength * pow(max(dot(normalDir, halfwayDir), 0.0), 2.0) * lightColor;

    vec4 sampled = texture(texture_diffuse, vTexCoord);
    vec3 objectColor = sampled.rgb;
    vec4 fragColor = vec4((ambient + diffuse + specular) * objectColor, sampled.a);

    uint pixelIndex = pixelPos.y * push.width + pixelPos.x;

    uint nodeIndex = atomicAdd(nodeCounter, 1u);
    if (nodeIndex >= fubo.maxNodes) {
        discard;
    }

    uint oldHead = atomicExchange(heads[pixelIndex], nodeIndex);
    nodes[nodeIndex].color = fragColor;
    nodes[nodeIndex].depth = gl_FragCoord.z;
    nodes[nodeIndex].next = oldHead;

    discard;
}
