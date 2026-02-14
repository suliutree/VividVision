#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inTangent;
layout(location = 3) in vec2 inUV;
layout(location = 4) in uvec4 inJoint;
layout(location = 5) in vec4 inWeight;

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

layout(push_constant) uniform DrawPush {
  mat4 model;
  vec4 baseColor;
  vec4 emissive;
  vec4 flags;
  vec4 mrAlpha;
} uDraw;

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vWorldNormal;
layout(location = 2) out vec2 vUV;
layout(location = 3) out vec4 vBaseColor;
layout(location = 4) out vec4 vEmissive;
layout(location = 5) out vec4 vMrAlpha;
layout(location = 6) out vec3 vWorldTangent;
layout(location = 7) out vec3 vWorldBitangent;
layout(location = 8) out vec4 vMaterialFlags;

void main() {
  uint boneOffset = uint(uDraw.mrAlpha.w + 0.5);
  mat4 skin =
      inWeight.x * uBones[boneOffset + inJoint.x] +
      inWeight.y * uBones[boneOffset + inJoint.y] +
      inWeight.z * uBones[boneOffset + inJoint.z] +
      inWeight.w * uBones[boneOffset + inJoint.w];

  vec4 localPos = skin * vec4(inPos, 1.0);

  mat3 skinLinear = mat3(skin);
  vec3 localNrm = normalize(skinLinear * inNormal);
  vec3 localTan = normalize(skinLinear * inTangent.xyz);
  localTan = normalize(localTan - localNrm * dot(localNrm, localTan));

  vec4 worldPos = uDraw.model * localPos;

  mat3 modelLinear = mat3(uDraw.model);
  mat3 modelNormal = transpose(inverse(modelLinear));
  vec3 worldNrm = normalize(modelNormal * localNrm);
  vec3 worldTan = normalize(modelLinear * localTan);
  worldTan = normalize(worldTan - worldNrm * dot(worldNrm, worldTan));
  vec3 worldBitan = normalize(cross(worldNrm, worldTan) * inTangent.w);

  vWorldPos = worldPos.xyz;
  vWorldNormal = worldNrm;
  vWorldTangent = worldTan;
  vWorldBitangent = worldBitan;
  vUV = inUV;
  vBaseColor = uDraw.baseColor;
  vEmissive = uDraw.emissive;
  vMaterialFlags = uDraw.flags;
  vMrAlpha = uDraw.mrAlpha;

  gl_Position = uFrame.proj * uFrame.view * worldPos;
}
