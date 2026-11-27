#version 430 core
layout(binding = 0) uniform sampler2D uMask;
layout(binding = 1) uniform sampler2D uDilated;  // uMask dilated horizontally by uWidth
uniform vec4 uColor;
uniform int uWidth;
out vec4 FragColor;

// Outside the highlighted silhouette, within uWidth pixels of it
void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    if (texelFetch(uMask, p, 0).r != 0.0)
        discard;
    int maxY = textureSize(uDilated, 0).y - 1;
    float near = 0.0;
    for (int y = -uWidth; y <= uWidth; ++y)
        near = max(near, texelFetch(uDilated, ivec2(p.x, clamp(p.y + y, 0, maxY)), 0).r);
    if (near == 0.0)
        discard;
    FragColor = uColor;
}
