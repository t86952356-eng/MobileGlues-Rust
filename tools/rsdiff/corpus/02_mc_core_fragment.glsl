#version 150

uniform sampler2D Sampler0;
uniform vec4 ColorModulator;
uniform float ScreenBrightness;

in vec2 texCoord0;
in vec4 vertexColor;

precision mediump float;
precision mediump int;

out highp vec4 outColor0;
out highp vec4 outColor1;
out highp vec4 outColor2;
out highp vec4 outColor3;
out highp vec4 outColor4;
out highp vec4 outColor5;
out highp vec4 outColor6;
out highp vec4 outColor7;

void main() {
    vec4 color = texture(Sampler0, texCoord0) * vertexColor * ColorModulator;
    outColor0 = color;
    outColor1 = color;
    outColor2 = color;
    outColor3 = color;
    outColor4 = color;
    outColor5 = color;
    outColor6 = color;
    outColor7 = color;
}
