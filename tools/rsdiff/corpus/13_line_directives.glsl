#version 330 compatibility
# define ATTRIB_IN in
#line 1 "minecraft:shaders/vertex.vsh"
#line 8
# line 12 "other.glsl"
   #line 20
 #line 31
ATTRIB_IN vec3 Position;
uniform mat4 ModelViewMat;
uniform vec4 Color;
#line 40
out vec4 vertexColor;
void main() {
    gl_Position = ModelViewMat * vec4(Position, 1.0);
    vertexColor = Color;
}
