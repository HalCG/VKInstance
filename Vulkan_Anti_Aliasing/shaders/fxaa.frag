#version 450

layout(location = 0) in vec2 vUV;
layout(binding = 0) uniform sampler2D uScreenTex;

layout(location = 0) out vec4 outColor;

float rgb2luma(vec3 rgb) {
    return sqrt(dot(rgb, vec3(0.299, 0.587, 0.114)));
}

void main() {
    vec2 texelSize = 1.0 / textureSize(uScreenTex, 0);

    float lumaCenter = rgb2luma(texture(uScreenTex, vUV).rgb);
    float lumaDown   = rgb2luma(texture(uScreenTex, vUV + vec2(0.0, -texelSize.y)).rgb);
    float lumaUp     = rgb2luma(texture(uScreenTex, vUV + vec2(0.0,  texelSize.y)).rgb);
    float lumaLeft   = rgb2luma(texture(uScreenTex, vUV + vec2(-texelSize.x, 0.0)).rgb);
    float lumaRight  = rgb2luma(texture(uScreenTex, vUV + vec2( texelSize.x, 0.0)).rgb);

    float lumaMin = min(lumaCenter, min(min(lumaDown, lumaUp), min(lumaLeft, lumaRight)));
    float lumaMax = max(lumaCenter, max(max(lumaDown, lumaUp), max(lumaLeft, lumaRight)));
    float lumaRange = lumaMax - lumaMin;

    if (lumaRange < max(0.0312, lumaMax * 0.125)) {
        outColor = texture(uScreenTex, vUV);
        return;
    }

    vec2 dir;
    dir.x = -((lumaLeft + lumaUp) - (lumaRight + lumaDown));
    dir.y =  ((lumaLeft + lumaDown) - (lumaRight + lumaUp));

    float dirReduce = max((lumaLeft + lumaRight + lumaDown + lumaUp) * (0.25 * 0.125), 0.0078125);
    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
    dir = min(vec2(8.0), max(vec2(-8.0), dir * rcpDirMin)) * texelSize;

    vec3 rgbA = 0.5 * (texture(uScreenTex, vUV + dir * (1.0/3.0 - 0.5)).rgb +
                      texture(uScreenTex, vUV + dir * (2.0/3.0 - 0.5)).rgb);
    vec3 rgbB = rgbA * 0.5 + 0.25 * (texture(uScreenTex, vUV + dir * -0.5).rgb +
                                     texture(uScreenTex, vUV + dir * 0.5).rgb);
    
    float lumaB = rgb2luma(rgbB);
    if (lumaB < lumaMin || lumaB > lumaMax) {
        outColor = vec4(rgbA, 1.0);
    } else {
        outColor = vec4(rgbB, 1.0);
    }
}
