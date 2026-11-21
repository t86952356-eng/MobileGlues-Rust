#version 100
// a comment mentioning #version 999 in text
precision mediump float;
uniform sampler2D tex;
void main() {
    gl_FragColor = texture2D(tex, vec2(0.5));
    // string "#version 330" inside a comment
    int n = 100 + 220;
    gl_FragColor *= float(n) / 320.0;
}
