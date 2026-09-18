#version 450

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

// 绑定 0：采样 2D Storage Image 转换后的 Combined Image Sampler
layout(set = 0, binding = 0) uniform sampler2D uVisualizationTexture;

void main() {
    // 对存储图像进行全屏采样，呈现最终 Nanite 伪彩可视化效果至窗口 Swapchain
    outColor = texture(uVisualizationTexture, inUV);
}
