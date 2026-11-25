#version 430 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;
layout(location = 3) in uint aDrawId;

layout(std430, binding = 0) readonly buffer Models {
    mat4 models[];
};

uniform mat4 uViewProj;

out vec3 vNormal;

void main() {
    mat4 model = models[aDrawId];
    vNormal = mat3(model) * aNormal;
    gl_Position = uViewProj * model * vec4(aPos, 1.0);
}
