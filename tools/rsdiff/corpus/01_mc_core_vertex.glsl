#version 150

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
