#version 430 core
layout(binding = 0) uniform sampler2D uTexture;

in vec2 vUv;
in vec4 vColor;
flat in uvec2 vFlagsEntity;

out uvec2 FragId;

void main() {
    if (texture(uTexture, vUv).a * vColor.a < 0.5)
        discard;
    FragId = uvec2(vFlagsEntity.y, 0u);
}
