#version 330
#extension GL_ARB_shader_texture_lod : enable
#extension GL_ARB_gpu_shader5 : enable

uniform sampler2D tex;
precision highp float;
in vec2 uv;
layout(location = 0) out vec4 fragColor;

void main() {
    fragColor = texture(tex, uv);
}
