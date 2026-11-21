#!/usr/bin/env python3
# Generates the rsdiff corpus: representative GLSL sources covering every
# pattern the translated passes touch, plus one large Iris-like shader for
# benchmarking. Deterministic — committed output, no randomness.
import os

OUT = os.path.join(os.path.dirname(__file__), "corpus")
os.makedirs(OUT, exist_ok=True)

def w(name, text):
    with open(os.path.join(OUT, name), "w", encoding="utf-8") as f:
        f.write(text)

# 1. Minecraft core-style vertex shader
w("01_mc_core_vertex.glsl", """#version 150

in vec3 Position;
in vec4 Color;
in vec2 UV0;
in ivec2 UV2;

uniform mat4 ModelViewMat;
uniform mat4 ProjMat;
uniform int FogStart;

out vertexColor {
    vec4 color;
    vec2 texCoord0;
    vec2 texCoord2;
    vec4 normal;
} vertex;

void main() {
    gl_Position = ProjMat * ModelViewMat * vec4(Position, 1.0);
    vertex.color = Color;
}
""")

# 2. Minecraft core-style fragment shader with outColor + precision lines
w("02_mc_core_fragment.glsl", """#version 150

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
""")

# 3. Iris composite-style shader: colortex uniforms, temporal filter call
w("03_iris_composite.glsl", """#version 330 compatibility

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
""")

# 4. samplerBuffer emulation — light (2-arg texelFetch forms)
w("04_samplerbuffer_light.glsl", """#version 400

uniform isamplerBuffer lightData;
uniform isamplerBuffer blockData;

flat in int lightIndex;
flat in int blockIndex;

layout(location = 0) out vec4 fragColor;

void main() {
    ivec4 light = texelFetch(lightData, lightIndex);
    ivec4 block = texelFetch(blockData, blockIndex );
    ivec4 mixed = texelFetch(lightData ,  lightIndex * 2  );
    fragColor = vec4(light) / 255.0 + vec4(block) / 255.0 + vec4(mixed) / 255.0;
}
""")

# 5. samplerBuffer emulation — mixed & edge-case texelFetch forms
w("05_samplerbuffer_mixed.glsl", """#version 400

uniform isamplerBuffer buf;
uniform isampler2D regularTex;

layout(location = 0) out vec4 fragColor;

ivec4 helper(isamplerBuffer b, int i) {
    return texelFetch(b, i);
}

void main() {
    ivec4 a = texelFetch(buf, 0);
    ivec4 b = texelFetch(buf,   );
    ivec4 c = texelFetch(buf, (a.x + b.y) * 2);
    ivec4 d = texelFetch(regularTex, ivec2(1, 2), 0);
    ivec4 e = texelFetch(buf, texelFetch(buf, 3).x);
    // nested calls and pre-existing ivec2 3-arg forms
    ivec4 f = texelFetch(buf, ivec2(a.x, a.y), 0);
    fragColor = vec4(a) + vec4(b) + vec4(c) + vec4(d) + vec4(e) + vec4(f);
}
""")

# 6. textureQueryLod usage — definition NOT yet present (needs injection probe)
w("06_texturequerylod.glsl", """#version 330

uniform sampler2D tex;
in vec2 uv;
layout(location = 0) out vec4 fragColor;

void main() {
    vec2 lod = textureQueryLod(tex, uv);
    fragColor = vec4(lod, 0.0, 1.0);
}
""")

# 7. version edge cases
w("07_version_edge.glsl", """#version 100
// a comment mentioning #version 999 in text
precision mediump float;
uniform sampler2D tex;
void main() {
    gl_FragColor = texture2D(tex, vec2(0.5));
    // string "#version 330" inside a comment
    int n = 100 + 220;
    gl_FragColor *= float(n) / 320.0;
}
""")

# 8. layout binding edge cases
w("08_layout_edge.glsl", """#version 430

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
""")

# 9. precision edge cases
w("09_precision_edge.glsl", """#version 330
#extension GL_ARB_shader_texture_lod : enable
#extension GL_ARB_gpu_shader5 : enable

uniform sampler2D tex;
precision highp float;
in vec2 uv;
layout(location = 0) out vec4 fragColor;

void main() {
    fragColor = texture(tex, uv);
}
""")

# 10. GI_TemporalFilter call without definition (triggers the injection path probe)
w("10_gi_temporal_call.glsl", """#version 330

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
""")

# 11. Large Iris-like shader for benchmarking (~50KB, deterministic)
big_parts = []
big_parts.append("#version 330 compatibility\n\n")
big_parts.append("uniform sampler2D colortex0;\n")
for i in range(1, 16):
    big_parts.append(f"uniform sampler2D colortex{i};\n")
big_parts.append("uniform sampler2D depthtex0;\nuniform sampler2D depthtex1;\n")
big_parts.append("uniform sampler2D noisetex;\nuniform vec2 pixelSize;\nuniform vec2 screenSize;\n")
big_parts.append("uniform float viewWidth;\nuniform float viewHeight;\nuniform int frameCounter;\n")
big_parts.append("uniform mat4 gbufferProjectionInverse;\nuniform mat4 gbufferModelViewInverse;\n")
big_parts.append("uniform mat4 gbufferPreviousProjection;\nuniform mat4 gbufferPreviousModelView;\n")
big_parts.append("uniform vec3 fogColor;\nuniform float rainStrength;\nuniform vec3 sunPosition;\n")
big_parts.append("uniform mat4 shadowProjection;\nuniform mat4 shadowModelView;\nuniform int worldTime;\n")
big_parts.append("\n#extension GL_ARB_shader_texture_lod : enable\n\n")
for i in range(40):
    big_parts.append(f"float noise_{i}(vec2 p) {{\n")
    big_parts.append("    vec2 i = floor(p);\n    vec2 f = fract(p);\n")
    big_parts.append("    f = f * f * (3.0 - 2.0 * f);\n")
    big_parts.append(f"    float n = texture(noisetex, i / 256.0 + vec2({i}.0, {i % 7}.0) / 256.0).x;\n")
    big_parts.append("    return mix(n, fract(n * 91.3458), f.x * f.y);\n}\n\n")
for i in range(24):
    big_parts.append(f"vec3 lighting_pass_{i}(vec3 albedo, vec3 normal, float depth) {{\n")
    big_parts.append(f"    vec3 s = albedo * ({i % 5}.5 + 0.5) / 6.2831;\n")
    big_parts.append(f"    s += dot(normal, sunPosition) * {i % 3}.25;\n")
    big_parts.append(f"    s *= exp(-depth * {1 + i % 4}.4);\n")
    for j in range(4):
        big_parts.append(f"    s += noise_{(i + j) % 40}(gl_FragCoord.xy * 0.{1 + j}) * 0.0{j};\n")
    big_parts.append("    return s;\n}\n\n")
big_parts.append("layout(location = 0) out vec4 fragColor0;\n")
big_parts.append("layout(location = 1) out vec4 fragColor1;\n")
big_parts.append("\nvoid main() {\n")
big_parts.append("    vec2 uv = gl_FragCoord.xy / screenSize;\n")
big_parts.append("    float depth = texture(depthtex0, uv).x;\n")
big_parts.append("    vec3 albedo = texture(colortex0, uv).rgb;\n")
big_parts.append("    vec3 normal = texture(colortex1, uv).rgb * 2.0 - 1.0;\n")
big_parts.append("    vec3 lit = vec3(0.0);\n")
for i in range(24):
    big_parts.append(f"    lit += lighting_pass_{i}(albedo, normal, depth);\n")
big_parts.append("    fragColor0 = vec4(lit / 24.0, 1.0);\n")
big_parts.append("    fragColor1 = vec4(depth, length(albedo), rainStrength, 1.0);\n")
big_parts.append("}\n")
w("11_big_iris_like.glsl", "".join(big_parts))

# 12. Large samplerBuffer-heavy shader for texelFetch benchmarking
sb = ["#version 400\n\n"]
sb.append("uniform isamplerBuffer chunkData;\n")
for i in range(16):
    sb.append(f"uniform isamplerBuffer buffer{i};\n")
sb.append("\nflat in int index;\nlayout(location = 0) out vec4 fragColor;\n\nvoid main() {\n")
sb.append("    ivec4 acc = texelFetch(chunkData, index);\n")
for i in range(16):
    sb.append(f"    acc += texelFetch(buffer{i}, index + {i});\n")
    sb.append(f"    acc -= texelFetch(buffer{i}, index * {i + 1});\n")
    sb.append(f"    acc ^= texelFetch(buffer{(i + 3) % 16}, index >> {i % 4});\n")
sb.append("    fragColor = vec4(acc) / 255.0;\n}\n")
w("12_big_samplerbuffer.glsl", "".join(sb))

print("corpus written:", len(os.listdir(OUT)), "files")
for f in sorted(os.listdir(OUT)):
    print(f"  {f}  ({os.path.getsize(os.path.join(OUT, f))} bytes)")
