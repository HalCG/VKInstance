// 与 OpenGL depth_peeling_render.frag 一致（深度剥离 + Blinn-Phong + 纹理 Alpha）

#version 450

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vTexCoord;

layout(binding = 1) uniform PeelFragUbo {
    vec4 cameraPos;
    vec4 lightPos;
    vec4 k;
    vec4 screenSize;
} fubo;

layout(binding = 2) uniform sampler2D texture_diffuse;
layout(binding = 3) uniform sampler2D texture_depth;

layout(location = 0) out vec4 outColor;

void main() {
    // 采样上一层剥离深度；z <= frontDepth 的片元已被处理，必须 discard
    vec2 depthUv = gl_FragCoord.xy / fubo.screenSize.xy;
    float frontDepth = texture(texture_depth, depthUv).r;

    if (gl_FragCoord.z <= frontDepth + 1e-6) {
        discard;
    }
    // 剩余片元经硬件深度测试（LESS）写入本层 outputDepth

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

    vec3 objectColor = vec3(0.8);
    float alpha = 0.0;
    if (vTexCoord.x >= 0.0 && vTexCoord.y >= 0.0) {
        vec4 sampled = texture(texture_diffuse, vTexCoord);
        objectColor = sampled.rgb;
        alpha = sampled.a;
    }

    outColor = vec4((ambient + diffuse + specular) * objectColor, alpha);
}
