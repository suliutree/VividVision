#version 450

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vWorldNormal;
layout(location = 2) in vec2 vUV;
layout(location = 3) in vec4 vBaseColor;
layout(location = 4) in vec4 vEmissive;
layout(location = 5) in vec4 vMrAlpha;
layout(location = 6) in vec3 vWorldTangent;
layout(location = 7) in vec3 vWorldBitangent;
layout(location = 8) in vec4 vMaterialFlags;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform FrameUBO {
  mat4 view;
  mat4 proj;
  mat4 lightViewProj;
  vec4 cameraPos;
  vec4 lightMeta;
  vec4 debugFlags;
  vec4 shadowMeta;
} uFrame;

struct LightGpu {
  vec4 positionRange;
  vec4 directionType;
  vec4 colorIntensity;
  vec4 coneCos;
};

layout(set = 0, binding = 1) readonly buffer LightBuffer {
  LightGpu lights[];
} uLights;

layout(set = 0, binding = 2) uniform sampler2D uIblEnvTex;
layout(set = 0, binding = 3) uniform sampler2D uShadowMap;

layout(set = 2, binding = 0) uniform sampler2D uBaseColorTex;
layout(set = 2, binding = 1) uniform sampler2D uMetalRoughTex;
layout(set = 2, binding = 2) uniform sampler2D uNormalTex;
layout(set = 2, binding = 3) uniform sampler2D uEmissiveTex;
layout(set = 2, binding = 4) uniform sampler2D uSpecularTex;
layout(set = 2, binding = 5) uniform sampler2D uMetallicTex;
layout(set = 2, binding = 6) uniform sampler2D uRoughnessTex;
layout(set = 2, binding = 7) uniform sampler2D uOcclusionTex;

const float kPi = 3.14159265359;

float saturate(float x) {
  return clamp(x, 0.0, 1.0);
}

float InterleavedGradientNoise(vec2 pixelPos) {
  float x = dot(pixelPos, vec2(0.06711056, 0.00583715));
  return fract(52.9829189 * fract(x));
}

vec3 LinearToSrgb(vec3 linearColor) {
  vec3 clamped = max(linearColor, vec3(0.0));
  vec3 low = clamped * 12.92;
  vec3 high = 1.055 * pow(clamped, vec3(1.0 / 2.4)) - 0.055;
  bvec3 useLow = lessThanEqual(clamped, vec3(0.0031308));
  return mix(high, low, useLow);
}

vec2 DirToLatLongUv(vec3 dir) {
  dir = normalize(dir);
  float u = atan(dir.z, dir.x) / (2.0 * kPi) + 0.5;
  float v = acos(clamp(dir.y, -1.0, 1.0)) / kPi;
  return vec2(fract(u), clamp(v, 0.0, 1.0));
}

vec3 SampleIbl(vec3 dir) {
  return texture(uIblEnvTex, DirToLatLongUv(dir)).rgb;
}

vec3 SampleIblDiffuse(vec3 N) {
  N = normalize(N);
  vec3 up = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
  vec3 T = normalize(cross(up, N));
  vec3 B = normalize(cross(N, T));
  float spread = 0.65;

  vec3 c0 = SampleIbl(N);
  vec3 c1 = SampleIbl(normalize(N + spread * T));
  vec3 c2 = SampleIbl(normalize(N - spread * T));
  vec3 c3 = SampleIbl(normalize(N + spread * B));
  vec3 c4 = SampleIbl(normalize(N - spread * B));
  vec3 c5 = SampleIbl(normalize(N + spread * (T + B) * 0.7071));
  vec3 c6 = SampleIbl(normalize(N + spread * (T - B) * 0.7071));
  vec3 c7 = SampleIbl(normalize(N + spread * (-T + B) * 0.7071));
  vec3 c8 = SampleIbl(normalize(N + spread * (-T - B) * 0.7071));
  return c0 * 0.22 + (c1 + c2 + c3 + c4) * 0.11 + (c5 + c6 + c7 + c8) * 0.085;
}

vec3 SampleIblSpecular(vec3 R, float roughness) {
  vec3 N = normalize(R);
  vec3 up = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
  vec3 T = normalize(cross(up, N));
  vec3 B = normalize(cross(N, T));
  float spread = max(roughness * roughness, 0.02) * 0.35;

  vec3 c0 = SampleIbl(N);
  vec3 c1 = SampleIbl(normalize(N + spread * T));
  vec3 c2 = SampleIbl(normalize(N - spread * T));
  vec3 c3 = SampleIbl(normalize(N + spread * B));
  vec3 c4 = SampleIbl(normalize(N - spread * B));
  return c0 * 0.40 + (c1 + c2 + c3 + c4) * 0.15;
}

vec2 EnvBrdfApprox(float roughness, float NdotV) {
  vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
  vec4 c1 = vec4(1.0, 0.0425, 1.040, -0.040);
  vec4 r = roughness * c0 + c1;
  float a004 = min(r.x * r.x, exp2(-9.28 * NdotV)) * r.x + r.y;
  return vec2(-1.04, 1.04) * a004 + r.zw;
}

vec3 NormalFromMap(float signedNormalScale) {
  vec3 tangentNormal = texture(uNormalTex, vUV).xyz * 2.0 - 1.0;
  if (signedNormalScale < 0.0) {
    tangentNormal.y = -tangentNormal.y;
  }
  tangentNormal.xy *= max(abs(signedNormalScale), 0.0001);
  tangentNormal = normalize(tangentNormal);
  mat3 tbn = mat3(normalize(vWorldTangent), normalize(vWorldBitangent), normalize(vWorldNormal));
  return normalize(tbn * tangentNormal);
}

vec3 EvalPbr(vec3 N, vec3 V, vec3 L, vec3 albedo, vec3 F0, float metallic, float roughness) {
  vec3 H = normalize(V + L);

  float NdotL = max(dot(N, L), 0.0);
  float NdotV = max(dot(N, V), 0.0);
  float NdotH = max(dot(N, H), 0.0);
  float VdotH = max(dot(V, H), 0.0);

  vec3 F = F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);

  float a = roughness * roughness;
  float a2 = a * a;
  float denom = (NdotH * NdotH) * (a2 - 1.0) + 1.0;
  float D = a2 / max(3.14159265 * denom * denom, 1e-5);

  float k = ((roughness + 1.0) * (roughness + 1.0)) * 0.125;
  float Gv = NdotV / max(NdotV * (1.0 - k) + k, 1e-5);
  float Gl = NdotL / max(NdotL * (1.0 - k) + k, 1e-5);
  float G = Gv * Gl;

  vec3 spec = (D * G * F) / max(4.0 * NdotV * NdotL, 1e-5);
  vec3 kd = (1.0 - F) * (1.0 - metallic);
  vec3 diff = kd * albedo / 3.14159265;

  return (diff + spec) * NdotL;
}

float EvalDirectionalShadow(vec3 N, vec3 L) {
  if (uFrame.shadowMeta.x < 0.5) {
    return 1.0;
  }

  vec4 lightClip = uFrame.lightViewProj * vec4(vWorldPos, 1.0);
  vec3 ndc = lightClip.xyz / max(lightClip.w, 1e-5);
  vec2 uv = ndc.xy * 0.5 + 0.5;
  float z = ndc.z;
  if (uv.x <= 0.0 || uv.x >= 1.0 || uv.y <= 0.0 || uv.y >= 1.0 || z <= 0.0 || z >= 1.0) {
    return 1.0;
  }

  float slopeBias = (1.0 - max(dot(N, L), 0.0)) * 0.0018;
  float bias = max(uFrame.shadowMeta.y, slopeBias);

  vec2 texel = 1.0 / vec2(textureSize(uShadowMap, 0));
  float pcfRadius = max(uFrame.shadowMeta.w, 1.0);
  float kernel[5] = float[5](1.0, 2.0, 3.0, 2.0, 1.0);
  float visibility = 0.0;
  float weightSum = 0.0;
  for (int y = -2; y <= 2; ++y) {
    for (int x = -2; x <= 2; ++x) {
      float w = kernel[x + 2] * kernel[y + 2];
      float depth = texture(uShadowMap, uv + vec2(float(x), float(y)) * texel * pcfRadius).r;
      visibility += ((z - bias <= depth) ? 1.0 : 0.0) * w;
      weightSum += w;
    }
  }
  float lit = visibility / max(weightSum, 1e-5);
  return mix(1.0, lit, clamp(uFrame.shadowMeta.z, 0.0, 1.0));
}

void main() {
  float useSpecGloss = step(0.5, min(vMaterialFlags.x, 1.0));
  float useGridOverlay = step(1.5, vMaterialFlags.x);
  float signedNormalScale = vMaterialFlags.y;
  float occlusionStrength = max(vMaterialFlags.z, 0.0);
  float useSeparateMR = vMaterialFlags.w;
  float enableNormalMap = uFrame.debugFlags.x;
  float enableSpecularIbl = uFrame.debugFlags.y;

  vec3 mapN = NormalFromMap(signedNormalScale);
  vec3 N = normalize(mix(normalize(vWorldNormal), mapN, step(0.5, enableNormalMap)));
  vec3 V = normalize(uFrame.cameraPos.xyz - vWorldPos);

  vec4 baseColorTexel = texture(uBaseColorTex, vUV);
  vec3 albedo = baseColorTexel.rgb * vBaseColor.rgb;
  float alpha = baseColorTexel.a * vBaseColor.a;

  vec3 mrPacked = texture(uMetalRoughTex, vUV).rgb;
  float metallicSample = mix(mrPacked.b, texture(uMetallicTex, vUV).r, step(0.5, useSeparateMR));
  float roughnessSample = mix(mrPacked.g, texture(uRoughnessTex, vUV).r, step(0.5, useSeparateMR));
  float metallic = saturate(vMrAlpha.x * metallicSample);
  float roughness = clamp(vMrAlpha.y * roughnessSample, 0.04, 1.0);

  if (useGridOverlay > 0.5) {
    vec2 worldXZ = vWorldPos.xz;
    vec2 grid1 = abs(fract(worldXZ) - 0.5);
    vec2 grid5 = abs(fract(worldXZ / 5.0) - 0.5);
    float fw = max(fwidth(worldXZ.x), fwidth(worldXZ.y));
    float line1 = 1.0 - smoothstep(0.0, fw * 1.35, min(grid1.x, grid1.y));
    float line5 = 1.0 - smoothstep(0.0, fw * 1.85, min(grid5.x, grid5.y));
    float gridMask = clamp(max(line1 * 0.60, line5), 0.0, 1.0);
    vec3 gridColor = vec3(0.10, 0.12, 0.15);
    albedo = mix(albedo, gridColor, gridMask);
    roughness = max(roughness, 0.86);
  }

  vec3 specularTexel = texture(uSpecularTex, vUV).rgb;
  vec3 f0MetalRough = mix(vec3(0.04), albedo, metallic);
  vec3 F0 = mix(f0MetalRough, specularTexel, useSpecGloss);
  F0 = clamp(F0, vec3(0.02), vec3(1.0));
  float NdotV = max(dot(N, V), 0.0);

  uint lightCount = uint(uFrame.lightMeta.x + 0.5);
  float ambientStrength = max(uFrame.lightMeta.y, 0.0);
  float exposure = max(uFrame.lightMeta.z, 0.01);
  float iblStrength = max(uFrame.lightMeta.w, 0.0);
  vec3 litAccum = vec3(0.0);

  for (uint i = 0; i < lightCount; ++i) {
    LightGpu light = uLights.lights[i];

    vec3 lightColor = light.colorIntensity.rgb * light.colorIntensity.a;
    float type = light.directionType.w;
    vec3 lightDir = normalize(light.directionType.xyz);

    vec3 L = vec3(0.0);
    float attenuation = 1.0;

    if (type < 0.5) {
      L = normalize(-lightDir);
      attenuation *= EvalDirectionalShadow(N, L);
    } else {
      vec3 toLight = light.positionRange.xyz - vWorldPos;
      float dist = length(toLight);
      L = dist > 1e-5 ? toLight / dist : vec3(0.0, 1.0, 0.0);

      float range = max(light.positionRange.w, 1e-3);
      float rangeFactor = 1.0 - pow(clamp(dist / range, 0.0, 1.0), 4.0);
      rangeFactor = rangeFactor * rangeFactor;
      attenuation = rangeFactor / max(1.0 + dist * dist, 1e-5);

      if (type > 1.5) {
        float cosTheta = dot(normalize(-L), lightDir);
        float innerCos = light.coneCos.x;
        float outerCos = light.coneCos.y;
        float cone = clamp((cosTheta - outerCos) / max(innerCos - outerCos, 1e-5), 0.0, 1.0);
        attenuation *= cone;
      }
    }

    vec3 brdf = EvalPbr(N, V, L, albedo, F0, metallic, roughness);
    litAccum += brdf * lightColor * attenuation;
  }

  float ao = mix(1.0, texture(uOcclusionTex, vUV).r, clamp(occlusionStrength, 0.0, 1.0));
  vec3 emissive = texture(uEmissiveTex, vUV).rgb * vEmissive.rgb * max(vEmissive.w, 0.0);
  vec3 Fv = F0 + (1.0 - F0) * pow(1.0 - NdotV, 5.0);
  vec3 kd = (1.0 - Fv) * (1.0 - metallic);
  vec3 diffuseDir = normalize(mix(normalize(vWorldNormal), N, 0.35));
  float hemiT = clamp(diffuseDir.y * 0.5 + 0.5, 0.0, 1.0);
  hemiT = smoothstep(0.0, 1.0, pow(hemiT, 0.90));
  vec3 hemiGround = vec3(0.18, 0.17, 0.16);
  vec3 hemiHorizon = vec3(0.50, 0.53, 0.58);
  vec3 hemiSky = vec3(0.70, 0.76, 0.84);
  float toHorizon = smoothstep(0.0, 0.62, hemiT);
  float toSky = smoothstep(0.38, 1.0, hemiT);
  vec3 hemiIrradiance = mix(hemiGround, hemiHorizon, toHorizon);
  hemiIrradiance = mix(hemiIrradiance, hemiSky, toSky);
  vec3 diffuseIbl = hemiIrradiance * albedo * kd;

  float iblRoughness = max(roughness, 0.35);
  vec3 R = reflect(-V, N);
  vec3 Rrough = normalize(mix(R, N, iblRoughness * iblRoughness));
  vec3 prefiltered = SampleIblSpecular(Rrough, iblRoughness);
  prefiltered = prefiltered / (prefiltered + vec3(1.0));
  vec2 envBrdf = EnvBrdfApprox(iblRoughness, NdotV);
  vec3 specularIbl = prefiltered * (F0 * envBrdf.x + envBrdf.y);
  specularIbl *= step(0.5, enableSpecularIbl);
  float specAoPow = exp2(-16.0 * iblRoughness - 1.0);
  float specAo = clamp(pow(max(NdotV + ao, 1e-3), specAoPow) - 1.0 + ao, 0.0, 1.0);
  vec3 ambient = (diffuseIbl * ao + specularIbl * specAo) * iblStrength + albedo * ambientStrength * ao;

  vec3 color = litAccum + ambient + emissive;
  if (vMrAlpha.z > 0.0 && alpha < vMrAlpha.z) {
    discard;
  }
  color = vec3(1.0) - exp(-color * exposure);
  color = LinearToSrgb(color);

  float outputLevels = max(uFrame.debugFlags.w, 16.0);
  float effectiveLevels = min(outputLevels, 255.0);
  float temporalPhase = fract(uFrame.debugFlags.z * 23.0);
  vec2 ditherBase = gl_FragCoord.xy + vec2(temporalPhase * 173.0, temporalPhase * 97.0);
  vec3 ditherNoise = vec3(InterleavedGradientNoise(ditherBase),
                          InterleavedGradientNoise(ditherBase + vec2(19.19, 73.42)),
                          InterleavedGradientNoise(ditherBase + vec2(47.77, 11.13))) - 0.5;
  float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
  float ditherStrength = mix(2.10, 1.20, clamp(luma, 0.0, 1.0));
  color += ditherNoise * (ditherStrength / effectiveLevels);
  color = clamp(color, vec3(0.0), vec3(1.0));

  outColor = vec4(color, alpha);
}
