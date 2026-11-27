#version 430 core
layout(binding = 0) uniform usampler2D uIds;  // .g = highlight
uniform vec4 uColor;
uniform int uWidth;
out vec4 FragColor;

// Outside the highlighted silhouette, within uWidth pixels of it
void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    ivec2 maxP = textureSize(uIds, 0) - 1;
    if (texelFetch(uIds, p, 0).g != 0u)
        discard;
    uint near = 0u;
    for (int y = -uWidth; y <= uWidth; ++y)
        for (int x = -uWidth; x <= uWidth; ++x)
            near |= texelFetch(uIds, clamp(p + ivec2(x, y), ivec2(0), maxP), 0).g;
    if (near == 0u)
        discard;
    FragColor = uColor;
}
