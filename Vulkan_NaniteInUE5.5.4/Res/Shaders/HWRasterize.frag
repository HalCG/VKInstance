#version 450
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : enable
#extension GL_EXT_shader_atomic_int64 : enable

layout(set = 0, binding = 0) uniform GlobalConstants {
    mat4 ProjectionMatrix;
    mat4 ViewMatrix;
    mat4 ModelMatrix;
    uvec4 Misc0;
    vec4 CameraPositionWS;
    vec4 ViewDirectionWS;
};

layout(std430, set = 0, binding = 3) buffer FVisBuffer64 {
    uint64_t mData[];
} VisBuffer64;

// 输入的 Cluster 打包 ID
layout(location = 0) flat in uvec4 V_PassThroughValue;

void main() {
    ivec2 texcoord = ivec2(gl_FragCoord.xy);
    float depth = gl_FragCoord.z;
    
    // 转换为 32 位无符号整数表示的 深度值 (使得 depth 保持严格大小关系可比性)
    uint64_t pixelDepth = floatBitsToUint(depth);
    int pixelIndex = texcoord.y * int(Misc0.x) + texcoord.x;
    
    // 打包为 64 位数据：高 32 位存储 Depth，低 32 位存储 ClusterID
    uint64_t pixelValue64 = (pixelDepth << 32) | uint64_t(V_PassThroughValue.x);
    
    // 采用 64 位 Atomic Min 原子操作写入 VisBuffer64，原子完成 深度测试 (Z-Test) + ClusterID 覆盖
    atomicMin(VisBuffer64.mData[pixelIndex], pixelValue64);
}
