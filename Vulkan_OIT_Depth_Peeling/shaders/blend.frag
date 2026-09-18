// Blend Pass 片元着色器
// 采样当前剥离层 peelColor_；Alpha≈0 的像素 discard，不参与混合
// 混合方程由管线配置：DST_ALPHA * SRC + DST（Front-to-Back OIT）

#version 450

layout(location = 0) in vec2 vUV;
layout(binding = 0) uniform sampler2D uLayerTex;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 layerColor = texture(uLayerTex, vUV);
    if (layerColor.a < 0.001) {
        discard;
    }
    outColor = layerColor;
}
