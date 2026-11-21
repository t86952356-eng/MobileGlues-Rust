#version 330 compatibility

uniform sampler2D colortex0;
uniform sampler2D colortex1;
uniform sampler2D colortex2;
uniform sampler2D colortex3;
uniform sampler2D colortex4;
uniform sampler2D colortex5;
uniform sampler2D colortex6;
uniform sampler2D colortex7;
uniform sampler2D depthtex0;
uniform sampler2D noisetex;
uniform vec2 pixelSize;
uniform vec2 screenSize;
uniform vec2 taaJitter;
uniform mat4 gbufferProjectionInverse;
uniform mat4 gbufferModelViewInverse;
uniform mat4 gbufferPreviousProjection;
uniform mat4 gbufferPreviousModelView;
uniform int frameCounter;
uniform float viewWidth;
uniform float viewHeight;
uniform int isEyeInWater;

/* DRAWBUFFERS:0123 */
layout(location = 0) out vec4 fragColor0;
layout(location = 1) out vec4 fragColor1;
layout(location = 2) out vec4 fragColor2;
layout(location = 3) out vec4 fragColor3;

vec3 GI_TemporalFilterCustom() {
    return vec3(0.0);
}

void main() {
    vec4 currentGI = texture(colortex0, gl_FragCoord.xy / screenSize);
    vec4 filtered = vec4(GI_TemporalFilterCustom(), 1.0);
    fragColor0 = currentGI;
    fragColor1 = filtered;
    fragColor2 = vec4(1.0);
    fragColor3 = vec4(0.0);
}
