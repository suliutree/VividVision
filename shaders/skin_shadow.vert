#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in uvec4 inJoint;
layout(location = 2) in vec4 inWeight;

layout(set = 0, binding = 0) uniform FrameUBO {
  mat4 view;
  mat4 proj;
  mat4 lightViewProj;
  vec4 cameraPos;
  vec4 lightMeta;
  vec4 debugFlags;
  vec4 shadowMeta;
} uFrame;

layout(set = 1, binding = 0) readonly buffer Bones {
  mat4 uBones[];
};

layout(push_constant) uniform ShadowPush {
  mat4 model;
  vec4 misc;
} uShadow;

void main() {
  uint boneOffset = uint(uShadow.misc.x + 0.5);
  mat4 skin =
      inWeight.x * uBones[boneOffset + inJoint.x] +
      inWeight.y * uBones[boneOffset + inJoint.y] +
      inWeight.z * uBones[boneOffset + inJoint.z] +
      inWeight.w * uBones[boneOffset + inJoint.w];

  vec4 worldPos = uShadow.model * (skin * vec4(inPos, 1.0));
  gl_Position = uFrame.lightViewProj * worldPos;
}
