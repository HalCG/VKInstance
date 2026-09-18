// deferred_lighting.vert — 全屏三角形（无需 VBO）
//
// 用 gl_VertexIndex 0/1/2 生成覆盖整个 NDC 的大三角形：
//   比两个三角形组成的四边形更高效（少一条对角线）
// vUV 输出 (0,0)-(1,0)-(0,1)，供片元着色器采样 G-Buffer

#version 450

layout(location = 0) out vec2 vUV;

void main() {
    vUV = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(vUV * 2.0 - 1.0, 0.0, 1.0);
}
