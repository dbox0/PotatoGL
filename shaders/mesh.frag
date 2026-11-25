#version 430 core
in vec3 vNormal;
out vec4 FragColor;

const vec3 lightDir = normalize(vec3(0.4, 1.0, 0.6));
const vec3 albedo = vec3(0.95, 0.75, 0.15);

void main() {
    float diffuse = max(dot(normalize(vNormal), lightDir), 0.0);
    FragColor = vec4(albedo * (0.25 + 0.75 * diffuse), 1.0);
}
