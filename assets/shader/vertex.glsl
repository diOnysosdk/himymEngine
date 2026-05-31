#version 330 compatibility

void main() {
    gl_Position = gl_Vertex;
    gl_TexCoord[0] = vec4(gl_Vertex.xy * 0.5 + 0.5, 0, 0);
}
