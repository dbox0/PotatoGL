#version 430 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;
layout(location = 3) in vec4 aColor;
layout(location = 4) in uvec2 aFlagsEntity;  // flags (1 = lit, 2 = receives shadows), entity id

uniform mat4 uViewProj;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUv;
out vec4 vColor;
flat out uvec2 vFlagsEntity;

void main() {
    vWorldPos = aPos;
    vNormal = aNormal;
    vUv = aUv;
    vColor = aColor;
    vFlagsEntity = aFlagsEntity;
    gl_Position = uViewProj * vec4(aPos, 1.0);
}
