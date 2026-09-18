// =============================================================================
// forward_plus.frag — Forward+ 片元着色器（Tile-Based Light Culling）
// =============================================================================
//
// 【与 forward.frag 的核心区别】
//   forward.frag:     for (i = 0; i < lightCount; ++i)  // 遍历全部 512 盏灯
//   forward_plus.frag: 只遍历「当前像素所在 Tile」的光源列表（最多 64 盏）
//
// 【Tile 如何确定】
//   tile = floor(gl_FragCoord.xy / tileSize)   // tileSize = 16（见 AppConfig::kTileSize）
//   tileIndex = tile.y * tilesX + tile.x
//
// 【SSBO 数据结构（set1）— 由 LightManager::buildForwardPlusTiles 每帧 CPU 填充】
//
//   binding0 LightBuffer:     PointLight lights[]     全部光源（与 Forward 相同）
//   binding1 TileCounts:      uint counts[tileCount]  每个 Tile 实际收录几盏灯
//   binding2 TileIndices:     uint indices[]          扁平数组，布局为：
//       indices[tileIndex * maxLightsPerTile + localIdx] = 全局 light 下标
//
// 【片元 UBO（set0 binding2）额外字段】
//   tilesX, tilesY, tileSize, maxLightsPerTile — 供上面索引计算
//
// 【光照模型】
//   与 forward.frag 相同：Blinn-Phong + 半径内 smoothstep 衰减 + 可选 HDR Reinhard
//
// 【性能特征】
//   CPU: 每帧 buildForwardPlusTiles O(灯数 × 覆盖 Tile 数)
//   GPU: 片元循环次数 ≈ 每 Tile 灯数（通常远小于 lightCount）
//
// =============================================================================

#version 450

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vTexCoords;

layout(set = 0, binding = 1) uniform sampler2D texture_diffuse1;

layout(set = 0, binding = 2) uniform ForwardPlusUbo {
    vec4 cameraPos;
    vec4 materialK;
    int lightCount;
    int tilesX;
    int tilesY;
    int tileSize;
    int maxLightsPerTile;
    int enableHDR;
} ubo;

struct PointLight {
    vec4 positionRadius;
    vec4 colorIntensity;
};

layout(set = 1, binding = 0, std430) readonly buffer LightBuffer {
    PointLight lights[];
};

layout(set = 1, binding = 1, std430) readonly buffer TileCounts {
    uint counts[];
};

layout(set = 1, binding = 2, std430) readonly buffer TileIndices {
    uint indices[];
};

layout(location = 0) out vec4 outColor;

void main() {
    // 屏幕像素 → Tile 坐标（与 CPU 侧 buildForwardPlusTiles 使用同一套 tileSize）
    ivec2 tile = ivec2(gl_FragCoord.xy) / ubo.tileSize;
    tile = clamp(tile, ivec2(0), ivec2(ubo.tilesX - 1, ubo.tilesY - 1));

    int tileIndex = tile.y * ubo.tilesX + tile.x;
    uint localCount = min(counts[tileIndex], uint(ubo.maxLightsPerTile));

    vec3 albedo = texture(texture_diffuse1, vTexCoords).rgb;
    vec3 normal = normalize(vNormal);
    vec3 viewDir = normalize(ubo.cameraPos.xyz - vWorldPos);
    vec3 result = ubo.materialK.x * albedo;

    for (uint i = 0u; i < localCount; ++i) {
        uint lightIndex = indices[tileIndex * ubo.maxLightsPerTile + int(i)];
        if (lightIndex >= uint(ubo.lightCount)) {
            continue;
        }

        vec3 lightPos = lights[lightIndex].positionRadius.xyz;
        float radius = lights[lightIndex].positionRadius.w;
        vec3 lightColor = lights[lightIndex].colorIntensity.rgb * lights[lightIndex].colorIntensity.w;

        vec3 lightDir = lightPos - vWorldPos;
        float dist = length(lightDir);
        if (dist > radius) {
            continue;
        }
        lightDir = normalize(lightDir);

        float attenuation = 1.0 - smoothstep(radius * 0.7, radius, dist);
        vec3 diffuse = ubo.materialK.y * max(dot(normal, lightDir), 0.0) * lightColor * albedo;
        vec3 halfway = normalize(lightDir + viewDir);
        vec3 specular = ubo.materialK.z * pow(max(dot(normal, halfway), 0.0), 32.0) * lightColor;
        result += (diffuse + specular) * attenuation;
    }

    if (ubo.enableHDR != 0) {
        result = result / (result + vec3(1.0));
        result = pow(result, vec3(1.0 / 2.2));
    }

    outColor = vec4(result, 1.0);
}
