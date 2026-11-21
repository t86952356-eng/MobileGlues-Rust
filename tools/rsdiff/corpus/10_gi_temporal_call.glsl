#version 330

uniform sampler2D colortex0;
uniform sampler2D colortex1;
uniform sampler2D depthtex0;
uniform vec2 screenSize;
uniform vec2 pixelSize;
uniform vec2 taaJitter;
uniform mat4 gbufferProjectionInverse;
uniform mat4 gbufferModelViewInverse;
uniform mat4 gbufferPreviousProjection;
uniform mat4 gbufferPreviousModelView;
uniform int frame;

layout(location = 0) out vec4 fragColor;

void main() {
    fragColor = GI_TemporalFilter();
}
