#version 400

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
