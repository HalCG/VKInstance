// Composite Pass：将累积色与背景合成到 Swapchain
// 对应 OpenGL depth_peeling_final.frag：
//   outColor = frontColor + background * frontColor.a

#version 450

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D uAccum;
layout(binding = 1) uniform CompositeUbo {
    vec4 backgroundColor;
} ubo;

void main() {
    vec4 frontColor = texture(uAccum, vUV);
    outColor = frontColor + vec4(ubo.backgroundColor.rgb, 1.0) * frontColor.a;
    outColor.a = 1.0;
}
