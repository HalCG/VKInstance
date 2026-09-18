// 全屏三角形顶点着色器（无 VBO，3 顶点生成覆盖整个屏幕的 UV）
// Blend / Composite Pass 共用

#version 450

layout(location = 0) out vec2 vUV;

void main() {
    vUV = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(vUV * 2.0 - 1.0, 0.0, 1.0);
}
