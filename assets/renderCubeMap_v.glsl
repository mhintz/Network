#version 410

in vec4 ciPosition;

out vec3 TexCoord0;

uniform mat4 ciModelViewProjection;

void main() {
  TexCoord0 = normalize(ciPosition.xyz);
  gl_Position = ciModelViewProjection * ciPosition;
}
