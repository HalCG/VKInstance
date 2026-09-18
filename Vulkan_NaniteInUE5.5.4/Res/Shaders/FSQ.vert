#version 450

// 输出全屏 UV 坐标 (0.0 .. 1.0)
layout(location = 0) out vec2 outUV;

// 无顶点缓冲区输入 (Zero-VB) 全屏三角形顶点着色器
void main() {
    // 利用 gl_VertexIndex (0, 1, 2) 硬件位运算在线生成覆盖全屏 NDC 的巨型三角形
    outUV = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(outUV * 2.0f - 1.0f, 0.0f, 1.0f);
}
