#version 430 core
in vec3 vNormal;
in vec2 vUv;
flat in uint vTextureLayer;
out vec4 FragColor;

uniform sampler2DArray uTextures;

const vec3 lightDir = normalize(vec3(0.4, 1.0, 0.6));

void main() {
    vec3 albedo = texture(uTextures, vec3(vUv, float(vTextureLayer))).rgb;
    float diffuse = max(dot(normalize(vNormal), lightDir), 0.0);
    FragColor = vec4(albedo * (0.25 + 0.75 * diffuse), 1.0);
}
