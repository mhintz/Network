#version 410

in vec3 TexCoord0;

out vec4 FragColor;

uniform samplerCube uCubeMap;

void main() {
  FragColor = texture(uCubeMap, TexCoord0);
}
