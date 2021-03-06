#version 410

layout (points, invocations = 6) in;
layout (points, max_vertices = 1) out;

in VertexData {
  vec4 gColor;
} gs_in[];

out vec4 aColor;

layout(std140) uniform uMatrices {
  mat4 viewProjectionMatrix[6];
};

void main() {
  gl_Layer = gl_InvocationID;

  for (int i = 0; i < gl_in.length(); i++) {
    aColor = gs_in[i].gColor;
    gl_Position = viewProjectionMatrix[gl_InvocationID] * gl_in[i].gl_Position;
    EmitVertex();
  }

  EndPrimitive();
}

