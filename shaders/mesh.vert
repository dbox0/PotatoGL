#version 430 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;
layout(location = 3) in uint aDrawId;

struct DrawData {
    vec4 rows[3];  // top three rows of the affine model matrix
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
    mat4x3 model = transpose(mat3x4(draw.rows[0], draw.rows[1], draw.rows[2]));

    // Cofactor matrix = inverse-transpose up to scale; correct under non-uniform scale
    mat3 m = mat3(model);
    mat3 cofactor = mat3(cross(m[1], m[2]), cross(m[2], m[0]), cross(m[0], m[1]));
    vNormal = cofactor * aNormal * sign(dot(m[0], cofactor[0]));

    vUv = aUv;
    vTextureLayer = draw.textureLayer;
    gl_Position = uViewProj * vec4(model * vec4(aPos, 1.0), 1.0);
}
