#version 400

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
