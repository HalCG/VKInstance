// deferred_lighting.frag — Deferred Pass2：全屏光照
//
// 从 G-Buffer 重建着色所需信息，对每个像素循环点光源（与 forward.frag 类似）。
// depth >= 0.9999 的像素视为天空/背景，输出固定暗色。
//
// Descriptor:
//   set0 binding0: LightingUbo (cameraPos, invView/invProj 预留, lightCount, HDR)
//   set0 binding1-4: G-Buffer 纹理 albedo / normal / position / depth
//   set1 binding0: SSBO PointLight[]
//
// 注意: invView/invProj 当前未用于位置重建（直接用 position RT），保留供扩展

#version 450

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform LightingUbo {
    vec4 cameraPos;
    mat4 invView;
    mat4 invProj;
    int lightCount;
    int enableHDR;
} ubo;

layout(set = 0, binding = 1) uniform sampler2D uGAlbedo;
layout(set = 0, binding = 2) uniform sampler2D uGNormal;
layout(set = 0, binding = 3) uniform sampler2D uGPosition;
layout(set = 0, binding = 4) uniform sampler2D uGDepth;

struct PointLight {
    vec4 positionRadius;
    vec4 colorIntensity;
};

layout(set = 1, binding = 0, std430) readonly buffer LightBuffer {
    PointLight lights[];
};

void main() {
    float depth = texture(uGDepth, vUV).r;
    if (depth >= 0.9999) {
        outColor = vec4(0.08, 0.09, 0.12, 1.0);
        return;
    }

    vec3 albedo = texture(uGAlbedo, vUV).rgb;
    vec3 normal = texture(uGNormal, vUV).rgb;
    if (dot(normal, normal) > 1e-6) {
        normal = normalize(normal);
    } else {
        normal = vec3(0.0, 1.0, 0.0);
    }
    vec3 worldPos = texture(uGPosition, vUV).xyz;
    const vec3 materialK = vec3(0.1, 0.7, 0.3);

    vec3 viewDir = normalize(ubo.cameraPos.xyz - worldPos);
    vec3 result = materialK.x * albedo;
    int count = min(ubo.lightCount, 512);

    for (int i = 0; i < count; ++i) {
        vec3 lightPos = lights[i].positionRadius.xyz;
        float radius = lights[i].positionRadius.w;
        vec3 lightColor = lights[i].colorIntensity.rgb * lights[i].colorIntensity.w;

        vec3 lightDir = lightPos - worldPos;
        float dist = length(lightDir);
        if (dist > radius) {
            continue;
        }
        lightDir = normalize(lightDir);

        float attenuation = 1.0 - smoothstep(radius * 0.7, radius, dist);
        vec3 diffuse = materialK.y * max(dot(normal, lightDir), 0.0) * lightColor * albedo;
        vec3 halfway = normalize(lightDir + viewDir);
        vec3 specular = materialK.z * pow(max(dot(normal, halfway), 0.0), 32.0) * lightColor;
        result += (diffuse + specular) * attenuation;
    }

    if (ubo.enableHDR != 0) {
        result = result / (result + vec3(1.0));
        result = pow(result, vec3(1.0 / 2.2));
    }

    outColor = vec4(result, 1.0);
}
