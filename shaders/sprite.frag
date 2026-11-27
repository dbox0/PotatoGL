#version 430 core
#include "common/lighting.glsl"

layout(binding = 0) uniform sampler2D uTexture;

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUv;
in vec4 vColor;  // sRGB
flat in uvec2 vFlagsEntity;

out vec4 FragColor;

vec3 srgbToLinear(vec3 c) {
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}

// Unlit: the texture times the color, as radiance. Lit: diffuse only, the normal facing the camera.
void main() {
    vec4 texel = texture(uTexture, vUv);
    vec4 color = vec4(texel.rgb * srgbToLinear(vColor.rgb), texel.a * vColor.a);
    if (color.a < 0.004)
        discard;
    if ((vFlagsEntity.x & 1u) != 0u) {
        Surface surface;
        surface.diffuse = color.rgb;
        surface.f0 = vec3(0.0);
        surface.shininess = 1.0;
        vec3 v = viewVector(vWorldPos);
        vec3 n = normalize(vNormal);
        if (dot(n, v) < 0.0)
            n = -n;
        color.rgb = surface.diffuse * hemisphere(n) +
                    directLighting(surface, vWorldPos, n, v, viewDepth(vWorldPos), (vFlagsEntity.x & 2u) != 0u);
    }
    FragColor = vec4(color.rgb * color.a, color.a);
}
