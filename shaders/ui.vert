#version 430 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUv;
layout(location = 2) in vec4 aColor;
layout(location = 3) in vec4 aColor2;
layout(location = 4) in vec4 aParams;
layout(location = 5) in uint aMode;

uniform vec2 uTargetSize;

out vec2 vUv;
out vec4 vColor;
out vec4 vColor2;
out vec4 vParams;
flat out uint vMode;

void main() {
    vec2 ndc = vec2(aPos.x / uTargetSize.x * 2.0 - 1.0, 1.0 - aPos.y / uTargetSize.y * 2.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
    vUv = aUv;
    vColor = aColor;
    vColor2 = aColor2;
    vParams = aParams;
    vMode = aMode;
}
