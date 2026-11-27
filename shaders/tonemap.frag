#version 430 core
layout(binding = 0) uniform sampler2D uScene;  // linear HDR
uniform float uExposure;
out vec4 FragColor;

// ACES filmic fit (Narkowicz 2015)
vec3 tonemap(vec3 c) {
    c *= uExposure;
    return clamp((c * (2.51 * c + 0.03)) / (c * (2.43 * c + 0.59) + 0.14), 0.0, 1.0);
}

vec3 fetch(vec2 uv) {
    return tonemap(textureLod(uScene, uv, 0.0).rgb);
}

// sqrt approximates the sRGB curve; FXAA wants perceptual luma
float luma(vec3 c) {
    return sqrt(dot(c, vec3(0.299, 0.587, 0.114)));
}

// FXAA on the tonemapped image, before sRGB encoding
void main() {
    vec2 texel = 1.0 / vec2(textureSize(uScene, 0));
    vec2 uv = gl_FragCoord.xy * texel;

    vec3 rgbM = fetch(uv);
    float lM = luma(rgbM);
    float lNW = luma(fetch(uv + vec2(-1.0, -1.0) * texel));
    float lNE = luma(fetch(uv + vec2( 1.0, -1.0) * texel));
    float lSW = luma(fetch(uv + vec2(-1.0,  1.0) * texel));
    float lSE = luma(fetch(uv + vec2( 1.0,  1.0) * texel));

    float lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE)));
    float lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));
    if (lMax - lMin < max(0.0312, lMax * 0.125)) {
        FragColor = vec4(rgbM, 1.0);
        return;
    }

    vec2 dir = vec2(-((lNW + lNE) - (lSW + lSE)), (lNW + lSW) - (lNE + lSE));
    float reduce = max((lNW + lNE + lSW + lSE) * (0.25 / 8.0), 1.0 / 128.0);
    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + reduce);
    dir = clamp(dir * rcpDirMin, -8.0, 8.0) * texel;

    vec3 rgbA = 0.5 * (fetch(uv + dir * (1.0 / 3.0 - 0.5)) + fetch(uv + dir * (2.0 / 3.0 - 0.5)));
    vec3 rgbB = rgbA * 0.5 + 0.25 * (fetch(uv - dir * 0.5) + fetch(uv + dir * 0.5));
    float lB = luma(rgbB);
    FragColor = vec4((lB < lMin || lB > lMax) ? rgbA : rgbB, 1.0);
}
