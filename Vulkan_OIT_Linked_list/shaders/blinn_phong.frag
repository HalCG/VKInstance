#version 450

// Pass 1：不透明 spot 的 Blinn-Phong 光照（alpha 恒为 1.0）
// 对应 OpenGL blinnPhong.frag

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vTexCoord;

layout(binding = 1) uniform FragUbo {
    vec4 cameraPos;
    vec4 lightPos;
    vec4 k; // x=ambient, y=diffuse, z=specular
} fubo;

layout(binding = 2) uniform sampler2D texture_diffuse;

layout(location = 0) out vec4 outColor;

void main() {
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
    if (vTexCoord.x >= 0.0 && vTexCoord.y >= 0.0) {
        objectColor = texture(texture_diffuse, vTexCoord).rgb;
    }

    outColor = vec4((ambient + diffuse + specular) * objectColor, 1.0);
}
