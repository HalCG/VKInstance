#version 450

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D currentTex;
layout(set = 0, binding = 1) uniform sampler2D historyTex;
layout(set = 0, binding = 2) uniform sampler2D depthTex;

layout(push_constant) uniform TaaParams {
    mat4 invViewProj;
    mat4 prevViewProj;
    vec2 jitterOffset;
    float blendFactor;
    int firstFrame;
} params;

vec3 RGB2YCoCg(vec3 rgb) {
    float y  =  0.25 * rgb.r + 0.5 * rgb.g + 0.25 * rgb.b;
    float co =  0.50 * rgb.r - 0.50 * rgb.b;
    float cg = -0.25 * rgb.r + 0.5 * rgb.g - 0.25 * rgb.b;
    return vec3(y, co, cg);
}

vec3 YCoCg2RGB(vec3 ycocg) {
    float y  = ycocg.x;
    float co = ycocg.y;
    float cg = ycocg.z;
    float r = y + co - cg;
    float g = y + cg;
    float b = y - co - cg;
    return vec3(r, g, b);
}

void main() {
    vec2 uv = inUV;
    vec2 unjitteredUV = uv - params.jitterOffset;
    vec4 currentColor = texture(currentTex, unjitteredUV);

    if (params.firstFrame != 0) {
        outColor = currentColor;
        return;
    }

    float depth = texture(depthTex, uv).r;
    vec4 ndcPos = vec4(uv * 2.0 - 1.0, depth, 1.0);

    vec4 worldPos = params.invViewProj * ndcPos;
    if (worldPos.w != 0.0) {
        worldPos /= worldPos.w;
    }

    vec4 prevNdcPos = params.prevViewProj * worldPos;
    if (prevNdcPos.w != 0.0) {
        prevNdcPos /= prevNdcPos.w;
    }

    vec2 prevUV = prevNdcPos.xy * 0.5 + 0.5;

    if (prevUV.x < 0.0 || prevUV.x > 1.0 || prevUV.y < 0.0 || prevUV.y > 1.0) {
        outColor = currentColor;
        return;
    }

    vec4 historyColor = texture(historyTex, prevUV);

    vec2 texelSize = 1.0 / vec2(textureSize(currentTex, 0));
    vec3 minColor = vec3(1e5);
    vec3 maxColor = vec3(-1e5);

    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            vec2 offset = vec2(x, y) * texelSize;
            vec3 neighborRGB = texture(currentTex, unjitteredUV + offset).rgb;
            vec3 neighborYCoCg = RGB2YCoCg(neighborRGB);
            minColor = min(minColor, neighborYCoCg);
            maxColor = max(maxColor, neighborYCoCg);
        }
    }

    vec3 historyYCoCg = RGB2YCoCg(historyColor.rgb);
    historyYCoCg = clamp(historyYCoCg, minColor, maxColor);
    vec3 clampedHistoryRGB = YCoCg2RGB(historyYCoCg);

    float alpha = params.blendFactor;
    vec3 blendedRGB = mix(clampedHistoryRGB, currentColor.rgb, alpha);

    outColor = vec4(blendedRGB, currentColor.a);
}
