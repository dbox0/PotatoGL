#version 430 core
layout(binding = 0) uniform sampler2D uTexture;

in vec2 vUv;
in vec4 vColor;
in vec4 vColor2;
in vec4 vParams;
flat in uint vMode;

out vec4 FragColor;

const uint ModeImage = 0u;
const uint ModeShape = 1u;
const uint ModeText = 2u;

vec3 linearToSrgb(vec3 c) {
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));
}

float roundedBox(vec2 p, vec2 halfSize, float radius) {
    vec2 q = abs(p) - halfSize + radius;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
}

// Colors are sRGB and stay sRGB: the target is written with GL_FRAMEBUFFER_SRGB off
void main() {
    vec4 color;
    if (vMode == ModeShape) {
        float d = roundedBox(vUv, vParams.xy, vParams.z);
        float aa = max(fwidth(d), 1e-4);
        float outside = clamp(0.5 - d / aa, 0.0, 1.0);
        float inner = clamp(0.5 - (d + vParams.w) / aa, 0.0, 1.0);
        color = vParams.w > 0.0 ? mix(vColor2, vColor, inner) : vColor;
        color.a *= outside;
    } else if (vMode == ModeText) {
        float d = texture(uTexture, vUv).r;
        float w = max(fwidth(d), 1e-4);
        color = vec4(vColor.rgb, vColor.a * smoothstep(0.5 - w, 0.5 + w, d));
    } else {
        vec4 texel = texture(uTexture, vUv);
        color = vec4(linearToSrgb(texel.rgb), texel.a) * vColor;
    }
    FragColor = vec4(color.rgb * color.a, color.a);
}
