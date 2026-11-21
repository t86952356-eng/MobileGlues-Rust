#version 430

layout(binding=0) uniform sampler2D tex0;
layout ( binding = 1 ) uniform sampler2D tex1;
layout(binding=2, set=1) uniform sampler2D tex2;
layout(binding = 3 , std140) uniform Block {
    vec4 member0;
    ivec4 member1;
} inst;
layout(
    binding = 4
)
uniform sampler2DArray texArr;
layout(location=5, binding=6) uniform sampler3D tex3;
layout(rgba8, binding=7) uniform image2D img;
layout(std430, binding=8) buffer SSBO { float data[]; } ssbo;
layout(binding=abc) uniform sampler2D notABinding;
layout(binding=) uniform sampler2D alsoNot;

out highp vec4 outColor0;
out highp vec4 outColor1;

void main() {
    outColor0 = texture(tex0, vec2(0.0));
    outColor1 = texture(tex1, vec2(0.0));
}
