#version 430 core
#include "common/lighting.glsl"

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUv;
flat in uint vMaterial;
out vec4 FragColor;

// MaterialData.h GpuMaterial
struct Material {
    vec4 baseColor;
    vec4 emission;
    vec4 uvTransform;  // tiling xy, offset zw
    float metallic;
    float smoothness;
    uint textureLayer;
    uint pad;
};

layout(std430, binding = 4) readonly buffer Materials {
    Material materials[];
};

uniform sampler2DArray uTextures;

void main() {
    Material mat = materials[vMaterial];
    vec2 uv = vUv * mat.uvTransform.xy + mat.uvTransform.zw;
    vec3 base = texture(uTextures, vec3(uv, float(mat.textureLayer))).rgb * mat.baseColor.rgb;
    Surface surface;
    surface.diffuse = base * (1.0 - mat.metallic);
    surface.f0 = mix(vec3(0.04), base, mat.metallic);
    surface.shininess = exp2(10.0 * mat.smoothness + 1.0);  // 2 .. 2048

    vec3 n = normalize(gl_FrontFacing ? vNormal : -vNormal);
    vec3 v = viewVector(vWorldPos);
    float depth = viewDepth(vWorldPos);

    // Ambient: the hemisphere seen by the normal, plus its reflection for glossy surfaces
    vec3 color = surface.diffuse * hemisphere(n) +
                 surface.f0 * hemisphere(reflect(-v, n)) * (mat.smoothness * mat.smoothness);
    color += mat.emission.rgb;
    color += directLighting(surface, vWorldPos, n, v, depth, true);

    FragColor = vec4(color, 1.0);
}
