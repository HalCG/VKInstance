// =============================================================================
// forward.frag — 经典前向渲染片元着色器
// =============================================================================
//
// 【性能特征 — 为何灯数敏感】
//   每个可见像素执行: for (i = 0; i < lightCount; ++i) { ... }
//   lightCount 可到 512 → 片元工作量 O(像素 × 灯数)
//   对比 Deferred：光照在 Pass2 全屏执行，成本与「屏幕像素×灯数」类似，但几何只画一次
//   对比 Forward+：片元只循环当前 Tile 内最多 64 盏灯
//
// 【光照模型】 Blinn-Phong
//   materialK.x = 环境光系数（乘以 albedo 作为底色）
//   materialK.y = 漫反射系数
//   materialK.z = 高光系数，pow(..., 32) 控制光泽锐度
//   半径内 attenuation = 1 - smoothstep(0.7r, r, dist)
//
// 【HDR】 enableHDR!=0 时 Reinhard: color/(color+1)，再 gamma 2.2
//
// 【Descriptor】
//   set0 binding1: combined image sampler (diffuse)
//   set0 binding2: ForwardFragUbo { cameraPos, materialK, lightCount, enableHDR }
//   set1 binding0: SSBO PointLight[]（LightManager::lightBuffer_）
//
// =============================================================================

#version 450

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vTexCoords;

layout(set = 0, binding = 1) uniform sampler2D texture_diffuse1;

layout(set = 0, binding = 2) uniform ForwardFragUbo {
    vec4 cameraPos;
    vec4 materialK;
    int lightCount;
    int enableHDR;
} fubo;

struct PointLight {
    vec4 positionRadius;
    vec4 colorIntensity;
};

layout(set = 1, binding = 0, std430) readonly buffer LightBuffer {
    PointLight lights[];
};

layout(location = 0) out vec4 outColor;

void main() {
    vec3 albedo = texture(texture_diffuse1, vTexCoords).rgb;
    vec3 normal = normalize(vNormal);
    vec3 viewDir = normalize(fubo.cameraPos.xyz - vWorldPos);
    vec3 result = fubo.materialK.x * albedo;
    int count = min(fubo.lightCount, 512);

    for (int i = 0; i < count; ++i) {
        vec3 lightPos = lights[i].positionRadius.xyz;
        float radius = lights[i].positionRadius.w;
        vec3 lightColor = lights[i].colorIntensity.rgb * lights[i].colorIntensity.w;

        vec3 lightDir = lightPos - vWorldPos;
        float dist = length(lightDir);
        if (dist > radius) {
            continue;
        }
        lightDir = normalize(lightDir);

        float attenuation = 1.0 - smoothstep(radius * 0.7, radius, dist);
        vec3 diffuse = fubo.materialK.y * max(dot(normal, lightDir), 0.0) * lightColor * albedo;
        vec3 halfway = normalize(lightDir + viewDir);
        vec3 specular = fubo.materialK.z * pow(max(dot(normal, halfway), 0.0), 32.0) * lightColor;
        result += (diffuse + specular) * attenuation;
    }

    if (fubo.enableHDR != 0) {
        result = result / (result + vec3(1.0));
        result = pow(result, vec3(1.0 / 2.2));
    }

    outColor = vec4(result, 1.0);
}
