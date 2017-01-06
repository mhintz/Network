#version 410

in vec4 ciPosition;
in vec4 ciColor;

out VertexData {
  vec4 gColor;
} vs_out;

uniform mat4 ciModelMatrix;

void main() {
  vs_out.gColor = ciColor;
  gl_Position = ciModelMatrix * ciPosition;
}
