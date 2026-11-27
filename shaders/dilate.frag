#version 430 core
layout(binding = 0) uniform sampler2D uMask;
uniform int uWidth;
out float Dilated;

// Horizontal half of the outline's box dilation
void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    int maxX = textureSize(uMask, 0).x - 1;
    float m = 0.0;
    for (int x = -uWidth; x <= uWidth; ++x)
        m = max(m, texelFetch(uMask, ivec2(clamp(p.x + x, 0, maxX), p.y), 0).r);
    Dilated = m;
}
