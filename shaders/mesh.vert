#version 430 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;
layout(location = 3) in uint aDrawId;

struct DrawData {
    mat4 model;
    uint textureLayer;
};

layout(std430, binding = 0) readonly buffer Draws {
    DrawData draws[];
};

uniform mat4 uViewProj;

out vec3 vNormal;
out vec2 vUv;
flat out uint vTextureLayer;

void main() {
    DrawData draw = draws[aDrawId];
    vNormal = mat3(draw.model) * aNormal;
    vUv = aUv;
    vTextureLayer = draw.textureLayer;
    gl_Position = uViewProj * draw.model * vec4(aPos, 1.0);
}
