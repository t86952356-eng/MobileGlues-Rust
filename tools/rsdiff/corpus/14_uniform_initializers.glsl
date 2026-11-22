#version 150
uniform sampler2D DiffuseSampler;
uniform sampler2D LightSampler;
uniform float AlphaCutout = 0.1;
uniform int Flags = 3;
uniform highp vec2 OutSize;
uniform vec4 ColorModulator = vec4(1.0, 1.0, 1.0, 1.0);
uniform mat4 TextureMat;
uniform float GameTime;
uniform int hasInitializer = 0;
uniform highp int packedCount = 12;
uniform vec4 highp oddOrder = vec4(0.0);
uniformity_check_scalar unused_word_marker;
in vec2 texCoord;
in vec4 vertexColor;
out vec4 fragColor;
void main() {
    vec4 diffuse = texture(DiffuseSampler, texCoord);
    float alpha = diffuse.a * vertexColor.a;
    if (alpha < AlphaCutout) {
        discard;
    }
    fragColor = diffuse * vertexColor * ColorModulator;
}
