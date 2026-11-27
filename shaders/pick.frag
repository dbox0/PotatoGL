#version 430 core
flat in uvec2 vEntity;
out uvec2 FragId;

void main() {
    FragId = vEntity;
}
