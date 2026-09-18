#version 450

// Pass 3：Composite — 对应 OpenGL composite.frag
//
// 1. 从 heads[pixel] 遍历 SSBO 链表
// 2. 按 depth 去重（三角形共享边重复片元）
// 3. 插入排序：depth 大者在前（远→近）
// 4. Back-to-Front Over：先 sample opaque，再逐层混合透明

#define MAX_FRAGMENTS 75
const float EPSILON = 0.0001;

layout(location = 0) in vec2 vUV;

struct Node {
    vec4 color;
    float depth;
    uint next;
};

layout(binding = 0) uniform sampler2D texture_opaque;

layout(std430, set = 0, binding = 1) buffer HeadPointerBuffer {
    uint heads[];
};

layout(std430, set = 0, binding = 2) buffer NodeStorage {
    Node nodes[];
};

layout(push_constant) uniform WindowInfo {
    uint width;
    uint height;
} windowInfo;

layout(location = 0) out vec4 outColor;

void main() {
    ivec2 pixelPos = ivec2(gl_FragCoord.xy);
    uint pixelIndex = pixelPos.y * windowInfo.width + pixelPos.x;

    Node frags[MAX_FRAGMENTS];
    int count = 0;
    uint idx = heads[pixelIndex];

    while (idx != 0xFFFFFFFFu && count < MAX_FRAGMENTS) {
        Node node = nodes[idx];
        bool isDuplicate = false;
        for (int i = 0; i < count; i++) {
            if (abs(frags[i].depth - node.depth) < EPSILON) {
                isDuplicate = true;
                break;
            }
        }
        if (!isDuplicate) {
            frags[count] = node;
            count++;
        }
        idx = node.next;
    }

    for (int i = 1; i < count; i++) {
        int j = i;
        Node toInsert = frags[i];
        while (j > 0 && toInsert.depth > frags[j - 1].depth) {
            frags[j] = frags[j - 1];
            j--;
        }
        frags[j] = toInsert;
    }

    vec4 color = texture(texture_opaque, vUV);
    for (int i = 0; i < count; i++) {
        color.rgb = color.rgb * (1.0 - frags[i].color.a) + frags[i].color.rgb * frags[i].color.a;
        color.a = color.a + frags[i].color.a * (1.0 - color.a);
    }

    outColor = color;
}
