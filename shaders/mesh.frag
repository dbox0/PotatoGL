#version 430 core
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUv;
flat in uint vTextureLayer;
out vec4 FragColor;

struct Light {
    vec4 positionInvRangeSq;
    vec4 colorSpotScale;
    vec4 directionSpotOffset;
};

layout(std430, binding = 1) readonly buffer Lights {
    Light lights[];
};

// Per cluster: offset and count into clusterLights
layout(std430, binding = 2) readonly buffer Clusters {
    uvec2 clusters[];
};

layout(std430, binding = 3) readonly buffer ClusterLights {
    uint clusterLights[];
};

const uvec3 ClusterGrid = uvec3(16, 9, 24);  // LightClusters::X, Y, Z

uniform sampler2DArray uTextures;
layout(binding = 1) uniform sampler2DShadow uShadowMap;

uniform vec3 uSunDirection;  // towards the light
uniform vec3 uSunRadiance;
uniform vec3 uSkyColor;
uniform vec3 uGroundColor;
uniform vec4 uEye;        // w = 1: camera position; w = 0: direction towards an orthographic camera
uniform vec4 uViewDepth;  // view-space depth = dot(xyz, worldPos) + w
uniform vec2 uClusterTile;  // tiles per pixel
uniform vec3 uClusterZ;     // slice = f(depth) * x + y, f = log when z = 1

uniform bool uShadows;
uniform mat4 uShadowMatrix;  // world to shadow map uv + depth
uniform float uShadowNormalOffset;
uniform float uShadowDistance;

const float Shininess = 32.0;
const float SpecularF0 = 0.04;

// Normalized Blinn-Phong: highlight energy stays constant as Shininess changes
vec3 shade(vec3 albedo, vec3 n, vec3 v, vec3 l, vec3 radiance) {
    float ndl = max(dot(n, l), 0.0);
    vec3 h = normalize(l + v);
    float spec = SpecularF0 * (Shininess + 8.0) * 0.125 * pow(max(dot(n, h), 0.0), Shininess);
    return (albedo + spec) * radiance * ndl;
}

// Inverse square, windowed to reach exactly 0 at the range (Karis 2013)
float distanceFalloff(float distSq, float invRangeSq) {
    float f = distSq * invRangeSq;
    float window = clamp(1.0 - f * f, 0.0, 1.0);
    return window * window / max(distSq, 1e-4);
}

// 4 hardware-PCF taps half a texel apart: a 3x3 tent filter. Normal offset instead of a large depth bias.
float sunShadow(vec3 n, float depth) {
    vec3 p = (uShadowMatrix * vec4(vWorldPos + n * uShadowNormalOffset, 1.0)).xyz;
    vec2 texel = 0.5 / vec2(textureSize(uShadowMap, 0));
    float lit = texture(uShadowMap, vec3(p.xy + vec2(-texel.x, -texel.y), p.z))
              + texture(uShadowMap, vec3(p.xy + vec2( texel.x, -texel.y), p.z))
              + texture(uShadowMap, vec3(p.xy + vec2(-texel.x,  texel.y), p.z))
              + texture(uShadowMap, vec3(p.xy + vec2( texel.x,  texel.y), p.z));
    return mix(lit * 0.25, 1.0, smoothstep(0.8 * uShadowDistance, uShadowDistance, depth));
}

void main() {
    vec3 albedo = texture(uTextures, vec3(vUv, float(vTextureLayer))).rgb;
    vec3 n = normalize(gl_FrontFacing ? vNormal : -vNormal);
    vec3 v = normalize(uEye.xyz - vWorldPos * uEye.w);
    float depth = dot(uViewDepth.xyz, vWorldPos) + uViewDepth.w;

    vec3 color = albedo * mix(uGroundColor, uSkyColor, n.y * 0.5 + 0.5);
    float shadow = uShadows ? sunShadow(n, depth) : 1.0;
    color += shade(albedo, n, v, uSunDirection, uSunRadiance * shadow);

    float slice = (uClusterZ.z > 0.5 ? log(max(depth, 1e-4)) : depth) * uClusterZ.x + uClusterZ.y;
    ivec3 cell = clamp(ivec3(ivec2(gl_FragCoord.xy * uClusterTile), int(floor(slice))),
                       ivec3(0), ivec3(ClusterGrid) - 1);
    uvec2 range = clusters[uint(cell.x) + ClusterGrid.x * (uint(cell.y) + ClusterGrid.y * uint(cell.z))];

    for (uint i = 0u; i < range.y; ++i) {
        Light light = lights[clusterLights[range.x + i]];
        vec3 toLight = light.positionInvRangeSq.xyz - vWorldPos;
        float distSq = dot(toLight, toLight);
        if (distSq * light.positionInvRangeSq.w >= 1.0)
            continue;
        vec3 l = toLight * inversesqrt(max(distSq, 1e-8));
        float cone = clamp(dot(-l, light.directionSpotOffset.xyz) * light.colorSpotScale.w +
                           light.directionSpotOffset.w, 0.0, 1.0);
        float attenuation = distanceFalloff(distSq, light.positionInvRangeSq.w) * cone * cone;
        color += shade(albedo, n, v, l, light.colorSpotScale.rgb * attenuation);
    }

    FragColor = vec4(color, 1.0);
}
