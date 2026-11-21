#version 330

uniform sampler2D tex;
in vec2 uv;
layout(location = 0) out vec4 fragColor;

void main() {
    vec2 lod = textureQueryLod(tex, uv);
    fragColor = vec4(lod, 0.0, 1.0);
}
