#version 330 compatibility

uniform sampler2D colortex0;
uniform sampler2D colortex1;
uniform sampler2D colortex2;
uniform sampler2D colortex3;
uniform sampler2D colortex4;
uniform sampler2D colortex5;
uniform sampler2D colortex6;
uniform sampler2D colortex7;
uniform sampler2D colortex8;
uniform sampler2D colortex9;
uniform sampler2D colortex10;
uniform sampler2D colortex11;
uniform sampler2D colortex12;
uniform sampler2D colortex13;
uniform sampler2D colortex14;
uniform sampler2D colortex15;
uniform sampler2D depthtex0;
uniform sampler2D depthtex1;
uniform sampler2D noisetex;
uniform vec2 pixelSize;
uniform vec2 screenSize;
uniform float viewWidth;
uniform float viewHeight;
uniform int frameCounter;
uniform mat4 gbufferProjectionInverse;
uniform mat4 gbufferModelViewInverse;
uniform mat4 gbufferPreviousProjection;
uniform mat4 gbufferPreviousModelView;
uniform vec3 fogColor;
uniform float rainStrength;
uniform vec3 sunPosition;
uniform mat4 shadowProjection;
uniform mat4 shadowModelView;
uniform int worldTime;

#extension GL_ARB_shader_texture_lod : enable

float noise_0(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n = texture(noisetex, i / 256.0 + vec2(0.0, 0.0) / 256.0).x;
    return mix(n, fract(n * 91.3458), f.x * f.y);
}

float noise_1(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n = texture(noisetex, i / 256.0 + vec2(1.0, 1.0) / 256.0).x;
    return mix(n, fract(n * 91.3458), f.x * f.y);
}

float noise_2(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n = texture(noisetex, i / 256.0 + vec2(2.0, 2.0) / 256.0).x;
    return mix(n, fract(n * 91.3458), f.x * f.y);
}

float noise_3(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n = texture(noisetex, i / 256.0 + vec2(3.0, 3.0) / 256.0).x;
    return mix(n, fract(n * 91.3458), f.x * f.y);
}

float noise_4(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n = texture(noisetex, i / 256.0 + vec2(4.0, 4.0) / 256.0).x;
    return mix(n, fract(n * 91.3458), f.x * f.y);
}

float noise_5(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n = texture(noisetex, i / 256.0 + vec2(5.0, 5.0) / 256.0).x;
    return mix(n, fract(n * 91.3458), f.x * f.y);
}

float noise_6(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n = texture(noisetex, i / 256.0 + vec2(6.0, 6.0) / 256.0).x;
    return mix(n, fract(n * 91.3458), f.x * f.y);
}

float noise_7(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n = texture(noisetex, i / 256.0 + vec2(7.0, 0.0) / 256.0).x;
    return mix(n, fract(n * 91.3458), f.x * f.y);
}

float noise_8(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n = texture(noisetex, i / 256.0 + vec2(8.0, 1.0) / 256.0).x;
    return mix(n, fract(n * 91.3458), f.x * f.y);
}

float noise_9(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n = texture(noisetex, i / 256.0 + vec2(9.0, 2.0) / 256.0).x;
    return mix(n, fract(n * 91.3458), f.x * f.y);
}

float noise_10(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n = texture(noisetex, i / 256.0 + vec2(10.0, 3.0) / 256.0).x;
    return mix(n, fract(n * 91.3458), f.x * f.y);
}

float noise_11(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n = texture(noisetex, i / 256.0 + vec2(11.0, 4.0) / 256.0).x;
    return mix(n, fract(n * 91.3458), f.x * f.y);
}

float noise_12(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n = texture(noisetex, i / 256.0 + vec2(12.0, 5.0) / 256.0).x;
    return mix(n, fract(n * 91.3458), f.x * f.y);
}

float noise_13(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n = texture(noisetex, i / 256.0 + vec2(13.0, 6.0) / 256.0).x;
    return mix(n, fract(n * 91.3458), f.x * f.y);
}

float noise_14(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n = texture(noisetex, i / 256.0 + vec2(14.0, 0.0) / 256.0).x;
    return mix(n, fract(n * 91.3458), f.x * f.y);
}

float noise_15(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n = texture(noisetex, i / 256.0 + vec2(15.0, 1.0) / 256.0).x;
    return mix(n, fract(n * 91.3458), f.x * f.y);
}

float noise_16(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n = texture(noisetex, i / 256.0 + vec2(16.0, 2.0) / 256.0).x;
    return mix(n, fract(n * 91.3458), f.x * f.y);
}

float noise_17(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n = texture(noisetex, i / 256.0 + vec2(17.0, 3.0) / 256.0).x;
    return mix(n, fract(n * 91.3458), f.x * f.y);
}

float noise_18(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n = texture(noisetex, i / 256.0 + vec2(18.0, 4.0) / 256.0).x;
    return mix(n, fract(n * 91.3458), f.x * f.y);
}

float noise_19(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n = texture(noisetex, i / 256.0 + vec2(19.0, 5.0) / 256.0).x;
    return mix(n, fract(n * 91.3458), f.x * f.y);
}

float noise_20(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n = texture(noisetex, i / 256.0 + vec2(20.0, 6.0) / 256.0).x;
    return mix(n, fract(n * 91.3458), f.x * f.y);
}

float noise_21(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n = texture(noisetex, i / 256.0 + vec2(21.0, 0.0) / 256.0).x;
    return mix(n, fract(n * 91.3458), f.x * f.y);
}

float noise_22(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n = texture(noisetex, i / 256.0 + vec2(22.0, 1.0) / 256.0).x;
    return mix(n, fract(n * 91.3458), f.x * f.y);
}

float noise_23(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n = texture(noisetex, i / 256.0 + vec2(23.0, 2.0) / 256.0).x;
    return mix(n, fract(n * 91.3458), f.x * f.y);
}

float noise_24(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n = texture(noisetex, i / 256.0 + vec2(24.0, 3.0) / 256.0).x;
    return mix(n, fract(n * 91.3458), f.x * f.y);
}

float noise_25(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n = texture(noisetex, i / 256.0 + vec2(25.0, 4.0) / 256.0).x;
    return mix(n, fract(n * 91.3458), f.x * f.y);
}

float noise_26(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n = texture(noisetex, i / 256.0 + vec2(26.0, 5.0) / 256.0).x;
    return mix(n, fract(n * 91.3458), f.x * f.y);
}

float noise_27(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n = texture(noisetex, i / 256.0 + vec2(27.0, 6.0) / 256.0).x;
    return mix(n, fract(n * 91.3458), f.x * f.y);
}

float noise_28(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n = texture(noisetex, i / 256.0 + vec2(28.0, 0.0) / 256.0).x;
    return mix(n, fract(n * 91.3458), f.x * f.y);
}

float noise_29(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n = texture(noisetex, i / 256.0 + vec2(29.0, 1.0) / 256.0).x;
    return mix(n, fract(n * 91.3458), f.x * f.y);
}

float noise_30(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n = texture(noisetex, i / 256.0 + vec2(30.0, 2.0) / 256.0).x;
    return mix(n, fract(n * 91.3458), f.x * f.y);
}

float noise_31(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n = texture(noisetex, i / 256.0 + vec2(31.0, 3.0) / 256.0).x;
    return mix(n, fract(n * 91.3458), f.x * f.y);
}

float noise_32(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n = texture(noisetex, i / 256.0 + vec2(32.0, 4.0) / 256.0).x;
    return mix(n, fract(n * 91.3458), f.x * f.y);
}

float noise_33(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n = texture(noisetex, i / 256.0 + vec2(33.0, 5.0) / 256.0).x;
    return mix(n, fract(n * 91.3458), f.x * f.y);
}

float noise_34(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n = texture(noisetex, i / 256.0 + vec2(34.0, 6.0) / 256.0).x;
    return mix(n, fract(n * 91.3458), f.x * f.y);
}

float noise_35(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n = texture(noisetex, i / 256.0 + vec2(35.0, 0.0) / 256.0).x;
    return mix(n, fract(n * 91.3458), f.x * f.y);
}

float noise_36(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n = texture(noisetex, i / 256.0 + vec2(36.0, 1.0) / 256.0).x;
    return mix(n, fract(n * 91.3458), f.x * f.y);
}

float noise_37(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n = texture(noisetex, i / 256.0 + vec2(37.0, 2.0) / 256.0).x;
    return mix(n, fract(n * 91.3458), f.x * f.y);
}

float noise_38(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n = texture(noisetex, i / 256.0 + vec2(38.0, 3.0) / 256.0).x;
    return mix(n, fract(n * 91.3458), f.x * f.y);
}

float noise_39(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n = texture(noisetex, i / 256.0 + vec2(39.0, 4.0) / 256.0).x;
    return mix(n, fract(n * 91.3458), f.x * f.y);
}

vec3 lighting_pass_0(vec3 albedo, vec3 normal, float depth) {
    vec3 s = albedo * (0.5 + 0.5) / 6.2831;
    s += dot(normal, sunPosition) * 0.25;
    s *= exp(-depth * 1.4);
    s += noise_0(gl_FragCoord.xy * 0.1) * 0.00;
    s += noise_1(gl_FragCoord.xy * 0.2) * 0.01;
    s += noise_2(gl_FragCoord.xy * 0.3) * 0.02;
    s += noise_3(gl_FragCoord.xy * 0.4) * 0.03;
    return s;
}

vec3 lighting_pass_1(vec3 albedo, vec3 normal, float depth) {
    vec3 s = albedo * (1.5 + 0.5) / 6.2831;
    s += dot(normal, sunPosition) * 1.25;
    s *= exp(-depth * 2.4);
    s += noise_1(gl_FragCoord.xy * 0.1) * 0.00;
    s += noise_2(gl_FragCoord.xy * 0.2) * 0.01;
    s += noise_3(gl_FragCoord.xy * 0.3) * 0.02;
    s += noise_4(gl_FragCoord.xy * 0.4) * 0.03;
    return s;
}

vec3 lighting_pass_2(vec3 albedo, vec3 normal, float depth) {
    vec3 s = albedo * (2.5 + 0.5) / 6.2831;
    s += dot(normal, sunPosition) * 2.25;
    s *= exp(-depth * 3.4);
    s += noise_2(gl_FragCoord.xy * 0.1) * 0.00;
    s += noise_3(gl_FragCoord.xy * 0.2) * 0.01;
    s += noise_4(gl_FragCoord.xy * 0.3) * 0.02;
    s += noise_5(gl_FragCoord.xy * 0.4) * 0.03;
    return s;
}

vec3 lighting_pass_3(vec3 albedo, vec3 normal, float depth) {
    vec3 s = albedo * (3.5 + 0.5) / 6.2831;
    s += dot(normal, sunPosition) * 0.25;
    s *= exp(-depth * 4.4);
    s += noise_3(gl_FragCoord.xy * 0.1) * 0.00;
    s += noise_4(gl_FragCoord.xy * 0.2) * 0.01;
    s += noise_5(gl_FragCoord.xy * 0.3) * 0.02;
    s += noise_6(gl_FragCoord.xy * 0.4) * 0.03;
    return s;
}

vec3 lighting_pass_4(vec3 albedo, vec3 normal, float depth) {
    vec3 s = albedo * (4.5 + 0.5) / 6.2831;
    s += dot(normal, sunPosition) * 1.25;
    s *= exp(-depth * 1.4);
    s += noise_4(gl_FragCoord.xy * 0.1) * 0.00;
    s += noise_5(gl_FragCoord.xy * 0.2) * 0.01;
    s += noise_6(gl_FragCoord.xy * 0.3) * 0.02;
    s += noise_7(gl_FragCoord.xy * 0.4) * 0.03;
    return s;
}

vec3 lighting_pass_5(vec3 albedo, vec3 normal, float depth) {
    vec3 s = albedo * (0.5 + 0.5) / 6.2831;
    s += dot(normal, sunPosition) * 2.25;
    s *= exp(-depth * 2.4);
    s += noise_5(gl_FragCoord.xy * 0.1) * 0.00;
    s += noise_6(gl_FragCoord.xy * 0.2) * 0.01;
    s += noise_7(gl_FragCoord.xy * 0.3) * 0.02;
    s += noise_8(gl_FragCoord.xy * 0.4) * 0.03;
    return s;
}

vec3 lighting_pass_6(vec3 albedo, vec3 normal, float depth) {
    vec3 s = albedo * (1.5 + 0.5) / 6.2831;
    s += dot(normal, sunPosition) * 0.25;
    s *= exp(-depth * 3.4);
    s += noise_6(gl_FragCoord.xy * 0.1) * 0.00;
    s += noise_7(gl_FragCoord.xy * 0.2) * 0.01;
    s += noise_8(gl_FragCoord.xy * 0.3) * 0.02;
    s += noise_9(gl_FragCoord.xy * 0.4) * 0.03;
    return s;
}

vec3 lighting_pass_7(vec3 albedo, vec3 normal, float depth) {
    vec3 s = albedo * (2.5 + 0.5) / 6.2831;
    s += dot(normal, sunPosition) * 1.25;
    s *= exp(-depth * 4.4);
    s += noise_7(gl_FragCoord.xy * 0.1) * 0.00;
    s += noise_8(gl_FragCoord.xy * 0.2) * 0.01;
    s += noise_9(gl_FragCoord.xy * 0.3) * 0.02;
    s += noise_10(gl_FragCoord.xy * 0.4) * 0.03;
    return s;
}

vec3 lighting_pass_8(vec3 albedo, vec3 normal, float depth) {
    vec3 s = albedo * (3.5 + 0.5) / 6.2831;
    s += dot(normal, sunPosition) * 2.25;
    s *= exp(-depth * 1.4);
    s += noise_8(gl_FragCoord.xy * 0.1) * 0.00;
    s += noise_9(gl_FragCoord.xy * 0.2) * 0.01;
    s += noise_10(gl_FragCoord.xy * 0.3) * 0.02;
    s += noise_11(gl_FragCoord.xy * 0.4) * 0.03;
    return s;
}

vec3 lighting_pass_9(vec3 albedo, vec3 normal, float depth) {
    vec3 s = albedo * (4.5 + 0.5) / 6.2831;
    s += dot(normal, sunPosition) * 0.25;
    s *= exp(-depth * 2.4);
    s += noise_9(gl_FragCoord.xy * 0.1) * 0.00;
    s += noise_10(gl_FragCoord.xy * 0.2) * 0.01;
    s += noise_11(gl_FragCoord.xy * 0.3) * 0.02;
    s += noise_12(gl_FragCoord.xy * 0.4) * 0.03;
    return s;
}

vec3 lighting_pass_10(vec3 albedo, vec3 normal, float depth) {
    vec3 s = albedo * (0.5 + 0.5) / 6.2831;
    s += dot(normal, sunPosition) * 1.25;
    s *= exp(-depth * 3.4);
    s += noise_10(gl_FragCoord.xy * 0.1) * 0.00;
    s += noise_11(gl_FragCoord.xy * 0.2) * 0.01;
    s += noise_12(gl_FragCoord.xy * 0.3) * 0.02;
    s += noise_13(gl_FragCoord.xy * 0.4) * 0.03;
    return s;
}

vec3 lighting_pass_11(vec3 albedo, vec3 normal, float depth) {
    vec3 s = albedo * (1.5 + 0.5) / 6.2831;
    s += dot(normal, sunPosition) * 2.25;
    s *= exp(-depth * 4.4);
    s += noise_11(gl_FragCoord.xy * 0.1) * 0.00;
    s += noise_12(gl_FragCoord.xy * 0.2) * 0.01;
    s += noise_13(gl_FragCoord.xy * 0.3) * 0.02;
    s += noise_14(gl_FragCoord.xy * 0.4) * 0.03;
    return s;
}

vec3 lighting_pass_12(vec3 albedo, vec3 normal, float depth) {
    vec3 s = albedo * (2.5 + 0.5) / 6.2831;
    s += dot(normal, sunPosition) * 0.25;
    s *= exp(-depth * 1.4);
    s += noise_12(gl_FragCoord.xy * 0.1) * 0.00;
    s += noise_13(gl_FragCoord.xy * 0.2) * 0.01;
    s += noise_14(gl_FragCoord.xy * 0.3) * 0.02;
    s += noise_15(gl_FragCoord.xy * 0.4) * 0.03;
    return s;
}

vec3 lighting_pass_13(vec3 albedo, vec3 normal, float depth) {
    vec3 s = albedo * (3.5 + 0.5) / 6.2831;
    s += dot(normal, sunPosition) * 1.25;
    s *= exp(-depth * 2.4);
    s += noise_13(gl_FragCoord.xy * 0.1) * 0.00;
    s += noise_14(gl_FragCoord.xy * 0.2) * 0.01;
    s += noise_15(gl_FragCoord.xy * 0.3) * 0.02;
    s += noise_16(gl_FragCoord.xy * 0.4) * 0.03;
    return s;
}

vec3 lighting_pass_14(vec3 albedo, vec3 normal, float depth) {
    vec3 s = albedo * (4.5 + 0.5) / 6.2831;
    s += dot(normal, sunPosition) * 2.25;
    s *= exp(-depth * 3.4);
    s += noise_14(gl_FragCoord.xy * 0.1) * 0.00;
    s += noise_15(gl_FragCoord.xy * 0.2) * 0.01;
    s += noise_16(gl_FragCoord.xy * 0.3) * 0.02;
    s += noise_17(gl_FragCoord.xy * 0.4) * 0.03;
    return s;
}

vec3 lighting_pass_15(vec3 albedo, vec3 normal, float depth) {
    vec3 s = albedo * (0.5 + 0.5) / 6.2831;
    s += dot(normal, sunPosition) * 0.25;
    s *= exp(-depth * 4.4);
    s += noise_15(gl_FragCoord.xy * 0.1) * 0.00;
    s += noise_16(gl_FragCoord.xy * 0.2) * 0.01;
    s += noise_17(gl_FragCoord.xy * 0.3) * 0.02;
    s += noise_18(gl_FragCoord.xy * 0.4) * 0.03;
    return s;
}

vec3 lighting_pass_16(vec3 albedo, vec3 normal, float depth) {
    vec3 s = albedo * (1.5 + 0.5) / 6.2831;
    s += dot(normal, sunPosition) * 1.25;
    s *= exp(-depth * 1.4);
    s += noise_16(gl_FragCoord.xy * 0.1) * 0.00;
    s += noise_17(gl_FragCoord.xy * 0.2) * 0.01;
    s += noise_18(gl_FragCoord.xy * 0.3) * 0.02;
    s += noise_19(gl_FragCoord.xy * 0.4) * 0.03;
    return s;
}

vec3 lighting_pass_17(vec3 albedo, vec3 normal, float depth) {
    vec3 s = albedo * (2.5 + 0.5) / 6.2831;
    s += dot(normal, sunPosition) * 2.25;
    s *= exp(-depth * 2.4);
    s += noise_17(gl_FragCoord.xy * 0.1) * 0.00;
    s += noise_18(gl_FragCoord.xy * 0.2) * 0.01;
    s += noise_19(gl_FragCoord.xy * 0.3) * 0.02;
    s += noise_20(gl_FragCoord.xy * 0.4) * 0.03;
    return s;
}

vec3 lighting_pass_18(vec3 albedo, vec3 normal, float depth) {
    vec3 s = albedo * (3.5 + 0.5) / 6.2831;
    s += dot(normal, sunPosition) * 0.25;
    s *= exp(-depth * 3.4);
    s += noise_18(gl_FragCoord.xy * 0.1) * 0.00;
    s += noise_19(gl_FragCoord.xy * 0.2) * 0.01;
    s += noise_20(gl_FragCoord.xy * 0.3) * 0.02;
    s += noise_21(gl_FragCoord.xy * 0.4) * 0.03;
    return s;
}

vec3 lighting_pass_19(vec3 albedo, vec3 normal, float depth) {
    vec3 s = albedo * (4.5 + 0.5) / 6.2831;
    s += dot(normal, sunPosition) * 1.25;
    s *= exp(-depth * 4.4);
    s += noise_19(gl_FragCoord.xy * 0.1) * 0.00;
    s += noise_20(gl_FragCoord.xy * 0.2) * 0.01;
    s += noise_21(gl_FragCoord.xy * 0.3) * 0.02;
    s += noise_22(gl_FragCoord.xy * 0.4) * 0.03;
    return s;
}

vec3 lighting_pass_20(vec3 albedo, vec3 normal, float depth) {
    vec3 s = albedo * (0.5 + 0.5) / 6.2831;
    s += dot(normal, sunPosition) * 2.25;
    s *= exp(-depth * 1.4);
    s += noise_20(gl_FragCoord.xy * 0.1) * 0.00;
    s += noise_21(gl_FragCoord.xy * 0.2) * 0.01;
    s += noise_22(gl_FragCoord.xy * 0.3) * 0.02;
    s += noise_23(gl_FragCoord.xy * 0.4) * 0.03;
    return s;
}

vec3 lighting_pass_21(vec3 albedo, vec3 normal, float depth) {
    vec3 s = albedo * (1.5 + 0.5) / 6.2831;
    s += dot(normal, sunPosition) * 0.25;
    s *= exp(-depth * 2.4);
    s += noise_21(gl_FragCoord.xy * 0.1) * 0.00;
    s += noise_22(gl_FragCoord.xy * 0.2) * 0.01;
    s += noise_23(gl_FragCoord.xy * 0.3) * 0.02;
    s += noise_24(gl_FragCoord.xy * 0.4) * 0.03;
    return s;
}

vec3 lighting_pass_22(vec3 albedo, vec3 normal, float depth) {
    vec3 s = albedo * (2.5 + 0.5) / 6.2831;
    s += dot(normal, sunPosition) * 1.25;
    s *= exp(-depth * 3.4);
    s += noise_22(gl_FragCoord.xy * 0.1) * 0.00;
    s += noise_23(gl_FragCoord.xy * 0.2) * 0.01;
    s += noise_24(gl_FragCoord.xy * 0.3) * 0.02;
    s += noise_25(gl_FragCoord.xy * 0.4) * 0.03;
    return s;
}

vec3 lighting_pass_23(vec3 albedo, vec3 normal, float depth) {
    vec3 s = albedo * (3.5 + 0.5) / 6.2831;
    s += dot(normal, sunPosition) * 2.25;
    s *= exp(-depth * 4.4);
    s += noise_23(gl_FragCoord.xy * 0.1) * 0.00;
    s += noise_24(gl_FragCoord.xy * 0.2) * 0.01;
    s += noise_25(gl_FragCoord.xy * 0.3) * 0.02;
    s += noise_26(gl_FragCoord.xy * 0.4) * 0.03;
    return s;
}

layout(location = 0) out vec4 fragColor0;
layout(location = 1) out vec4 fragColor1;

void main() {
    vec2 uv = gl_FragCoord.xy / screenSize;
    float depth = texture(depthtex0, uv).x;
    vec3 albedo = texture(colortex0, uv).rgb;
    vec3 normal = texture(colortex1, uv).rgb * 2.0 - 1.0;
    vec3 lit = vec3(0.0);
    lit += lighting_pass_0(albedo, normal, depth);
    lit += lighting_pass_1(albedo, normal, depth);
    lit += lighting_pass_2(albedo, normal, depth);
    lit += lighting_pass_3(albedo, normal, depth);
    lit += lighting_pass_4(albedo, normal, depth);
    lit += lighting_pass_5(albedo, normal, depth);
    lit += lighting_pass_6(albedo, normal, depth);
    lit += lighting_pass_7(albedo, normal, depth);
    lit += lighting_pass_8(albedo, normal, depth);
    lit += lighting_pass_9(albedo, normal, depth);
    lit += lighting_pass_10(albedo, normal, depth);
    lit += lighting_pass_11(albedo, normal, depth);
    lit += lighting_pass_12(albedo, normal, depth);
    lit += lighting_pass_13(albedo, normal, depth);
    lit += lighting_pass_14(albedo, normal, depth);
    lit += lighting_pass_15(albedo, normal, depth);
    lit += lighting_pass_16(albedo, normal, depth);
    lit += lighting_pass_17(albedo, normal, depth);
    lit += lighting_pass_18(albedo, normal, depth);
    lit += lighting_pass_19(albedo, normal, depth);
    lit += lighting_pass_20(albedo, normal, depth);
    lit += lighting_pass_21(albedo, normal, depth);
    lit += lighting_pass_22(albedo, normal, depth);
    lit += lighting_pass_23(albedo, normal, depth);
    fragColor0 = vec4(lit / 24.0, 1.0);
    fragColor1 = vec4(depth, length(albedo), rainStrength, 1.0);
}
