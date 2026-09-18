#version 450
/**
 * @file stochastic.frag
 * @brief Stochastic Transparency 片段着色器
 *
 * 核心原理（与 OpenGL quad.frag 一致）：
 *
 * 1. 子像素多重采样掩码 (gl_SampleMask[])：
 *    多采样 RenderPass 下 Fragment Shader 可写入 gl_SampleMask[0]，
 *    以 bitmask 形式控制当前片段在 N 个 MSAA 采样点上的 Pass/Fail。
 *    Vulkan 等价于 OpenGL 的 glEnable(GL_SAMPLE_MASK) + gl_SampleMask[0] 赋值。
 *
 * 2. Alpha → 概率采样 (Probability Sampling)：
 *    设 coverage = texture.a。对每个子采样点 i，生成 rand(i, frameID) ∈ [0,1)。
 *    若 r < coverage，则 bit i 置 1（保留该子像素）；否则置 0。
 *    期望上 N 个采样点中约有 N * Alpha 个被开启。
 *
 * 3. 深度与 Resolve：
 *    透明/不透明物体均正常深度测试与写入；各 MSAA 子采样独立 Depth。
 *    RenderPass Resolve 阶段硬件对 N 个子采样求平均，无需 Back-to-Front 排序。
 *
 * 4. MSAA 不可用 (sampleCnt <= 1) 时：
 *    回退到 discard 随机剔除，保证无 MSAA 的 GPU 仍可运行（噪声更大）。
 */

layout(location = 0) in vec2 vTexCoord;

layout(push_constant) uniform PushConstants {
    mat4 model;
    int frameID;
    int sampleCnt;
} push;

layout(binding = 1) uniform sampler2D texture_diffuse;

layout(location = 0) out vec4 outColor;

// 经典 GLSL 伪随机：输入 seed，输出 [0.0, 1.0)
float rand(vec2 seed) {
    return fract(sin(dot(seed, vec2(12.9898, 78.233))) * 43758.5453);
}

void main() {
    vec4 color = texture(texture_diffuse, vTexCoord);
    float coverage = color.a;

    // 无 MSAA 回退路径
    if (push.sampleCnt <= 1) {
        float r = rand(vec2(float(push.frameID), gl_FragCoord.x + gl_FragCoord.y));
        if (r >= coverage) {
            discard;
        }
        outColor = color;
        return;
    }

    // Stochastic Transparency 核心：Alpha 作为子采样命中概率
    uint randMask = 0u;
    for (int i = 0; i < push.sampleCnt; ++i) {
        vec2 seed = vec2(float(i), float(push.frameID));
        float r = rand(seed);
        if (r < coverage) {
            randMask |= (1u << i);
        }
    }

    // gl_SampleMask[0] 控制第 0~31 个 MSAA 采样点（16x MSAA 足够）
    gl_SampleMask[0] = int(randMask);
    outColor = color;
}
