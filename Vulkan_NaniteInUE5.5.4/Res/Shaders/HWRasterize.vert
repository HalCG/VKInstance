#version 450

// 绑定 0：全局 Matrix 与 Camera 矩阵 UBO
layout(set = 0, binding = 0) uniform GlobalConstants {
    mat4 ProjectionMatrix;
    mat4 ViewMatrix;
    mat4 ModelMatrix;
    uvec4 Misc0;
    vec4 CameraPositionWS;
    vec4 ViewDirectionWS;
};

// 绑定 1：Nanite 原始顶点与 Cluster 数据 SSBO
layout(std430, set = 0, binding = 1) readonly buffer FNaniteMesh {
    uint mData[];
} NaniteMesh;

// 绑定 2：可见 Cluster 列表 SSBO
layout(std430, set = 0, binding = 2) readonly buffer FVisiableClusterSWHW {
    uint mData[];
} VisiableClusterSWHW;

// 输出至 Fragment Shader 的 Flat 不插值数据 (x 位域: PageIndex << 8 | ClusterIndex + 1)
layout(location = 0) flat out uvec4 V_PassThroughValue;

// Cluster 解包信息
struct ClusterInfo {
    uint mBaseOffset;
    uint mIndexOffset;
    uint mIndexCount;
    vec4 mLODBounds;
    float mLODError;
    float mEdgeLength;
};

ClusterInfo GetClusterInfo(uint inPageIndex, uint inClusterIndex) {
    uint pageCount = NaniteMesh.mData[0];
    uint pageBaseOffsetInBytes = NaniteMesh.mData[1 + inPageIndex];
    uint pageBaseOffset = pageBaseOffsetInBytes / 4;
    uint clusterCountOnPage = NaniteMesh.mData[pageBaseOffset];
    uint clusterBaseOffsetInBytes = NaniteMesh.mData[pageBaseOffset + 1 + inClusterIndex];
    uint clusterBaseOffset = pageBaseOffset + 1 + clusterCountOnPage + clusterBaseOffsetInBytes / 4;
    uint clusterIndexOffset = NaniteMesh.mData[clusterBaseOffset] / 4;
    uint clusterIndexCount = NaniteMesh.mData[clusterBaseOffset + 1];
    uvec4 lodBounds = uvec4(
        NaniteMesh.mData[clusterBaseOffset + 2u],
        NaniteMesh.mData[clusterBaseOffset + 3u],
        NaniteMesh.mData[clusterBaseOffset + 4u],
        NaniteMesh.mData[clusterBaseOffset + 5u]
    );
    uint lodErrorAndEdgeLength = NaniteMesh.mData[clusterBaseOffset + 6];
    ClusterInfo clusterInfo;
    clusterInfo.mBaseOffset = clusterBaseOffset;
    clusterInfo.mIndexOffset = clusterIndexOffset;
    clusterInfo.mIndexCount = clusterIndexCount;
    clusterInfo.mLODBounds = uintBitsToFloat(lodBounds);
    vec2 unpackedData  = unpackHalf2x16(lodErrorAndEdgeLength);
    clusterInfo.mLODError = unpackedData.x;
    clusterInfo.mEdgeLength = unpackedData.y;
    return clusterInfo;
}

void main() {
    // gl_VertexIndex: 当前 Cluster 内的相对顶点 ID (0..383)
    // gl_InstanceIndex: 间接绘制实例化 ID，代表第几个可见 Cluster
    uint vertexIndex = gl_VertexIndex;
    uint clusterIndexWithInvoke = gl_InstanceIndex;

    // 从 VisiableClusterSWHW SSBO 中读取当前 Instance 对应的 pageIndex 与 clusterIndex
    uint pageIndex = VisiableClusterSWHW.mData[clusterIndexWithInvoke * 2];
    uint clusterIndex = VisiableClusterSWHW.mData[clusterIndexWithInvoke * 2 + 1];
    
    ClusterInfo clusterInfo = GetClusterInfo(pageIndex, clusterIndex);
    vec4 positionCS = vec4(0.0f, 0.0f, 0.0f, 0.0f);

    // 从 SSBO 动态解包读取 3D 顶点位置
    if (vertexIndex < clusterInfo.mIndexCount) {
        uint currentVertexIndexOffsetBase = clusterInfo.mBaseOffset + clusterInfo.mIndexOffset;
        uint currentVertexIndexOffset = currentVertexIndexOffsetBase + vertexIndex;
        uint currentIndexInCluster = NaniteMesh.mData[currentVertexIndexOffset];
        
        uint currentClusterPositionOffsetBase = clusterInfo.mBaseOffset + 7u;
        uint currentVertexPositionDataOffset = currentClusterPositionOffsetBase + 3 * currentIndexInCluster;
        vec3 positionMS = uintBitsToFloat(
            uvec3(
                NaniteMesh.mData[currentVertexPositionDataOffset],
                NaniteMesh.mData[currentVertexPositionDataOffset + 1],
                NaniteMesh.mData[currentVertexPositionDataOffset + 2]
            )
        );

        // MVP 矩阵变换
        vec4 positionWS = ModelMatrix * vec4(positionMS, 1.0f);
        positionWS.xyz = positionWS.xyz - CameraPositionWS.xyz;
        vec4 positionVS = ViewMatrix * positionWS;
        positionCS = ProjectionMatrix * positionVS;

        // 打包 PageIndex 与 ClusterIndex 传给 片元着色器
        V_PassThroughValue.x = (pageIndex << 8) | (clusterIndex + 1);
    }

    // Vulkan NDC 坐标系 Y 轴翻转适配 (OpenGL 坐标系为 Y 轴朝上，Vulkan 为 Y 轴朝下)
    positionCS.y = -positionCS.y;
    gl_Position = positionCS;
}
