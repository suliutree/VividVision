#include "asset/import/AssimpFbxImporter.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <optional>
#include <unordered_map>
#include <utility>

#include <assimp/Importer.hpp>
#include <assimp/config.h>
#include <assimp/GltfMaterial.h>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <glm/gtc/quaternion.hpp>

#include "asset/mesh/SkinWeight.hpp"
#include "asset/texture/ImageLoader.hpp"

namespace vv {
namespace {

constexpr TextureId kDefaultWhiteSrgb = 0;
constexpr TextureId kDefaultBlackLinear = 1;
constexpr TextureId kDefaultNormal = 2;
constexpr TextureId kDefaultSpecular = 3;
constexpr TextureId kDefaultWhiteLinear = 4;

struct SceneConversion {
  Mat4 c{1.0F};
  Mat4 cInv{1.0F};
  glm::mat3 r{1.0F};
  glm::mat3 rInv{1.0F};
  float unitScale = 1.0F;
};

Mat4 ToMat4(const aiMatrix4x4& m) {
  Mat4 out(1.0F);
  out[0][0] = m.a1;
  out[1][0] = m.a2;
  out[2][0] = m.a3;
  out[3][0] = m.a4;
  out[0][1] = m.b1;
  out[1][1] = m.b2;
  out[2][1] = m.b3;
  out[3][1] = m.b4;
  out[0][2] = m.c1;
  out[1][2] = m.c2;
  out[2][2] = m.c3;
  out[3][2] = m.c4;
  out[0][3] = m.d1;
  out[1][3] = m.d2;
  out[2][3] = m.d3;
  out[3][3] = m.d4;
  return out;
}

Vec3 ToVec3(const aiVector3D& v) {
  return Vec3(v.x, v.y, v.z);
}

Quat ToQuat(const aiQuaternion& q) {
  return Quat(q.w, q.x, q.y, q.z);
}

Transform DecomposeTransform(const Mat4& m) {
  Transform t;
  t.translation = Vec3(m[3]);

  Vec3 basisX = Vec3(m[0]);
  Vec3 basisY = Vec3(m[1]);
  Vec3 basisZ = Vec3(m[2]);

  t.scale.x = glm::length(basisX);
  t.scale.y = glm::length(basisY);
  t.scale.z = glm::length(basisZ);

  constexpr float kEps = 1e-8F;
  if (t.scale.x <= kEps || t.scale.y <= kEps || t.scale.z <= kEps) {
    t.rotation = Quat(1.0F, 0.0F, 0.0F, 0.0F);
    t.scale = Vec3(1.0F);
    return t;
  }

  basisX /= t.scale.x;
  basisY /= t.scale.y;
  basisZ /= t.scale.z;

  glm::mat3 rot(1.0F);
  rot[0] = basisX;
  rot[1] = basisY;
  rot[2] = basisZ;

  if (glm::determinant(rot) < 0.0F) {
    t.scale.x = -t.scale.x;
    rot[0] = -rot[0];
  }

  t.rotation = glm::normalize(glm::quat_cast(rot));
  return t;
}

int GetMetaInt(const aiScene* scene, const char* key, int fallback) {
  if (scene->mMetaData == nullptr) {
    return fallback;
  }
  int value = fallback;
  if (scene->mMetaData->Get(key, value)) {
    return value;
  }
  return fallback;
}

float GetMetaFloat(const aiScene* scene, const char* key, float fallback) {
  if (scene->mMetaData == nullptr) {
    return fallback;
  }
  float value = fallback;
  if (scene->mMetaData->Get(key, value)) {
    return value;
  }
  return fallback;
}

SceneConversion BuildConversion(const aiScene* srcScene, const ImportOptions& opt) {
  SceneConversion conv;

  const int rightAxis = GetMetaInt(srcScene, "CoordAxis", 0);
  const int rightSign = GetMetaInt(srcScene, "CoordAxisSign", 1);
  const int upAxis = GetMetaInt(srcScene, "UpAxis", 1);
  const int upSign = GetMetaInt(srcScene, "UpAxisSign", 1);
  const int frontAxis = GetMetaInt(srcScene, "FrontAxis", 2);
  const int frontSign = GetMetaInt(srcScene, "FrontAxisSign", -1);

  glm::mat3 rot(0.0F);
  rot[rightAxis][0] = static_cast<float>(rightSign);
  rot[upAxis][1] = static_cast<float>(upSign);
  rot[frontAxis][2] = static_cast<float>(-frontSign);  // target uses +Z back

  if (!opt.forceRightHanded) {
    rot = glm::mat3(1.0F);
  }

  float unitScale = 1.0F;
  if (opt.convertToMeters) {
    const float unitScaleFactor = GetMetaFloat(srcScene, "UnitScaleFactor", 1.0F);
    unitScale = unitScaleFactor * 0.01F;
  }

  conv.unitScale = unitScale;
  conv.r = rot;
  conv.rInv = glm::inverse(rot);
  conv.c = glm::scale(Mat4(1.0F), Vec3(unitScale)) * Mat4(rot);
  conv.cInv = glm::inverse(conv.c);
  return conv;
}

Vec3 NormalizeSafe(const Vec3& v, const Vec3& fallback = Vec3(0.0F, 1.0F, 0.0F)) {
  const float len2 = glm::dot(v, v);
  if (len2 <= 1e-12F) {
    return fallback;
  }
  return glm::normalize(v);
}

Vec3 ConvertDirection(const SceneConversion& conv, const Vec3& v) {
  return NormalizeSafe(conv.r * v);
}

Vec3 ConvertPosition(const SceneConversion& conv, const Vec3& v) {
  const Vec4 p = conv.c * Vec4(v, 1.0F);
  return Vec3(p);
}

Quat ConvertRotation(const SceneConversion& conv, const Quat& q) {
  const Mat4 m = glm::mat4_cast(q);
  const Mat4 m2 = Mat4(conv.r) * m * Mat4(conv.rInv);
  return glm::normalize(glm::quat_cast(m2));
}

struct ImportContext {
  const aiScene* src = nullptr;
  SceneConversion conv;
  Scene dst;
  std::filesystem::path sourceDir;

  std::unordered_map<const aiNode*, NodeId> nodeMap;
  std::unordered_map<std::string, NodeId> nodeByName;
  std::unordered_map<uint32_t, NodeId> meshNode;
  std::unordered_map<std::string, TextureId> textureMap;
  std::unordered_map<std::string, std::filesystem::path> textureFileIndex;
  bool textureFileIndexBuilt = false;
};

TextureId AddDefaultTexture(ImportContext& ctx, const std::string& name) {
  Texture tex;
  tex.uri = name;
  tex.format = PixelFormat::kR8G8B8A8;
  tex.width = 1;
  tex.height = 1;
  tex.srgb = true;
  tex.pixels = {255, 255, 255, 255};
  if (name.find("black") != std::string::npos) {
    tex.srgb = false;
    tex.pixels = {0, 0, 0, 255};
  }
  if (name.find("linear_white") != std::string::npos) {
    tex.srgb = false;
    tex.pixels = {255, 255, 255, 255};
  }
  if (name.find("normal") != std::string::npos) {
    tex.srgb = false;
    tex.pixels = {128, 128, 255, 255};
  }
  if (name.find("spec") != std::string::npos) {
    tex.srgb = true;
    tex.pixels = {56, 56, 56, 255};  // ~0.04 linear reflectance in sRGB
  }
  ctx.dst.textures.push_back(std::move(tex));
  return static_cast<TextureId>(ctx.dst.textures.size() - 1);
}

std::string NormalizeTextureUri(const std::string& uri) {
  std::string normalized = uri;
  std::replace(normalized.begin(), normalized.end(), '\\', '/');
  while (!normalized.empty() &&
         (normalized.front() == ' ' || normalized.front() == '\t' || normalized.front() == '\n' ||
          normalized.front() == '\r' || normalized.front() == '"' || normalized.front() == '\'')) {
    normalized.erase(normalized.begin());
  }
  while (!normalized.empty() &&
         (normalized.back() == ' ' || normalized.back() == '\t' || normalized.back() == '\n' ||
          normalized.back() == '\r' || normalized.back() == '"' || normalized.back() == '\'')) {
    normalized.pop_back();
  }

  std::string lowered = normalized;
  std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  constexpr const char* kFilePrefix = "file://";
  if (lowered.rfind(kFilePrefix, 0) == 0) {
    normalized = normalized.substr(7);
    if (normalized.size() >= 3 && normalized[0] == '/' &&
        std::isalpha(static_cast<unsigned char>(normalized[1])) && normalized[2] == ':') {
      normalized.erase(normalized.begin());
    }
  }
  return normalized;
}

std::string MakeTextureCacheKey(const std::string& normalizedUri, bool srgb) {
  std::string key = normalizedUri;
  key += srgb ? "|srgb" : "|linear";
  return key;
}

std::string ToLowerAscii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

void BuildTextureFileIndex(ImportContext& ctx) {
  if (ctx.textureFileIndexBuilt) {
    return;
  }
  ctx.textureFileIndexBuilt = true;

  std::error_code ec;
  std::filesystem::recursive_directory_iterator it(ctx.sourceDir, std::filesystem::directory_options::skip_permission_denied, ec);
  if (ec) {
    return;
  }
  for (const auto& entry : it) {
    if (!entry.is_regular_file()) {
      continue;
    }
    const std::string lowerName = ToLowerAscii(entry.path().filename().string());
    ctx.textureFileIndex.try_emplace(lowerName, entry.path());
  }
}

std::optional<std::filesystem::path> ResolveTexturePath(ImportContext& ctx, const std::string& normalizedUri) {
  if (normalizedUri.empty()) {
    return std::nullopt;
  }

  std::error_code ec;
  const std::filesystem::path inputPath(normalizedUri);
  if (inputPath.is_absolute() && std::filesystem::exists(inputPath, ec) && !ec) {
    return inputPath;
  }

  const std::filesystem::path localPath = ctx.sourceDir / inputPath;
  ec.clear();
  if (std::filesystem::exists(localPath, ec) && !ec) {
    return localPath;
  }

  const std::filesystem::path filename = inputPath.filename();
  if (!filename.empty()) {
    const std::filesystem::path siblingPath = ctx.sourceDir / filename;
    ec.clear();
    if (std::filesystem::exists(siblingPath, ec) && !ec) {
      return siblingPath;
    }

    BuildTextureFileIndex(ctx);
    const auto indexed = ctx.textureFileIndex.find(ToLowerAscii(filename.string()));
    if (indexed != ctx.textureFileIndex.end()) {
      return indexed->second;
    }
  }

  return std::nullopt;
}

TextureId AppendDecodedTexture(ImportContext& ctx,
                               const std::string& textureKey,
                               const std::string& textureUri,
                               const ImageRgba8& decoded,
                               bool srgb) {
  Texture tex;
  tex.uri = textureUri;
  tex.width = decoded.width;
  tex.height = decoded.height;
  tex.pixels = decoded.pixels;
  tex.srgb = srgb;
  tex.format = srgb ? PixelFormat::kR8G8B8A8_SRGB : PixelFormat::kR8G8B8A8;
  ctx.dst.textures.push_back(std::move(tex));
  const TextureId id = static_cast<TextureId>(ctx.dst.textures.size() - 1);
  ctx.textureMap[textureKey] = id;
  return id;
}

std::optional<TextureId> TryLoadEmbeddedTexture(ImportContext& ctx,
                                                const std::string& textureKey,
                                                const std::string& cacheKey,
                                                bool srgb) {
  if (ctx.src == nullptr || ctx.src->mNumTextures == 0) {
    return std::nullopt;
  }

  const aiTexture* embedded = ctx.src->GetEmbeddedTexture(textureKey.c_str());
  if (embedded == nullptr) {
    return std::nullopt;
  }

  if (embedded->mHeight == 0) {
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(embedded->pcData);
    const auto decoded = LoadImageRgba8FromMemory(bytes, static_cast<size_t>(embedded->mWidth));
    if (!decoded.has_value()) {
      return std::nullopt;
    }
    return AppendDecodedTexture(ctx, cacheKey, textureKey, *decoded, srgb);
  }

  if (embedded->mWidth == 0 || embedded->mHeight == 0) {
    return std::nullopt;
  }

  Texture tex;
  tex.uri = textureKey;
  tex.width = embedded->mWidth;
  tex.height = embedded->mHeight;
  tex.srgb = srgb;
  tex.format = srgb ? PixelFormat::kR8G8B8A8_SRGB : PixelFormat::kR8G8B8A8;
  const size_t pixelCount = static_cast<size_t>(tex.width) * static_cast<size_t>(tex.height);
  tex.pixels.resize(pixelCount * 4);
  for (size_t i = 0; i < pixelCount; ++i) {
    const aiTexel& src = embedded->pcData[i];
    tex.pixels[i * 4 + 0] = src.r;
    tex.pixels[i * 4 + 1] = src.g;
    tex.pixels[i * 4 + 2] = src.b;
    tex.pixels[i * 4 + 3] = src.a;
  }

  ctx.dst.textures.push_back(std::move(tex));
  const TextureId id = static_cast<TextureId>(ctx.dst.textures.size() - 1);
  ctx.textureMap[cacheKey] = id;
  return id;
}

TextureId GetOrCreateTexture(ImportContext& ctx, const std::string& uri, bool srgb, TextureId fallback) {
  if (uri.empty()) {
    return fallback;
  }

  const std::string normalizedUri = NormalizeTextureUri(uri);
  if (normalizedUri.empty()) {
    return fallback;
  }
  const std::string cacheKey = MakeTextureCacheKey(normalizedUri, srgb);

  const auto found = ctx.textureMap.find(cacheKey);
  if (found != ctx.textureMap.end()) {
    return found->second;
  }

  if (const auto embeddedId = TryLoadEmbeddedTexture(ctx, normalizedUri, cacheKey, srgb); embeddedId.has_value()) {
    return *embeddedId;
  }

  const auto texturePath = ResolveTexturePath(ctx, normalizedUri);
  if (!texturePath.has_value()) {
    ctx.textureMap[cacheKey] = fallback;
    return fallback;
  }

  const auto decoded = LoadImageRgba8(texturePath->string());
  if (!decoded.has_value()) {
    ctx.textureMap[cacheKey] = fallback;
    return fallback;
  }

  return AppendDecodedTexture(ctx, cacheKey, texturePath->string(), *decoded, srgb);
}

std::string GetFirstTexturePath(const aiMaterial* mat, std::initializer_list<aiTextureType> types) {
  for (aiTextureType type : types) {
    if (mat->GetTextureCount(type) == 0) {
      continue;
    }
    aiString path;
    if (mat->GetTexture(type, 0, &path) == aiReturn_SUCCESS && path.length > 0) {
      return path.C_Str();
    }
  }
  return {};
}

NodeId BuildNodesRecursive(ImportContext& ctx, const aiNode* srcNode, NodeId parent) {
  Node node;
  node.name = srcNode->mName.C_Str();
  node.parent = parent;

  const Mat4 srcLocal = ToMat4(srcNode->mTransformation);
  const Mat4 dstLocal = ctx.conv.c * srcLocal * ctx.conv.cInv;
  node.localBind = DecomposeTransform(dstLocal);
  node.localCurrent = node.localBind;
  node.worldCurrent = Mat4(1.0F);

  const NodeId id = static_cast<NodeId>(ctx.dst.nodes.size());
  ctx.dst.nodes.push_back(std::move(node));
  ctx.nodeMap[srcNode] = id;
  ctx.nodeByName[ctx.dst.nodes[id].name] = id;

  if (parent == kInvalidNodeId) {
    ctx.dst.roots.push_back(id);
  } else {
    ctx.dst.nodes[parent].children.push_back(id);
  }

  for (unsigned i = 0; i < srcNode->mNumMeshes; ++i) {
    const uint32_t meshIndex = srcNode->mMeshes[i];
    ctx.meshNode.try_emplace(meshIndex, id);
  }

  for (unsigned i = 0; i < srcNode->mNumChildren; ++i) {
    BuildNodesRecursive(ctx, srcNode->mChildren[i], id);
  }
  return id;
}

void ImportMaterials(ImportContext& ctx) {
  if (ctx.src->mNumMaterials == 0) {
    Material fallback;
    fallback.name = "DefaultMaterial";
    fallback.baseColorTex = kDefaultWhiteSrgb;
    fallback.metallicRoughnessTex = kDefaultWhiteLinear;
    fallback.metallicTex = kDefaultBlackLinear;
    fallback.roughnessTex = kDefaultWhiteLinear;
    fallback.normalTex = kDefaultNormal;
    fallback.occlusionTex = kDefaultWhiteLinear;
    fallback.emissiveTex = kDefaultBlackLinear;
    fallback.specularTex = kDefaultSpecular;
    ctx.dst.materials.push_back(std::move(fallback));
    return;
  }

  for (unsigned i = 0; i < ctx.src->mNumMaterials; ++i) {
    const aiMaterial* mat = ctx.src->mMaterials[i];

    Material out;
    aiString name;
    if (mat->Get(AI_MATKEY_NAME, name) == aiReturn_SUCCESS) {
      out.name = name.C_Str();
    } else {
      out.name = "Material_" + std::to_string(i);
    }

    aiColor4D baseColor(1.0F, 1.0F, 1.0F, 1.0F);
    if (mat->Get(AI_MATKEY_BASE_COLOR, baseColor) != aiReturn_SUCCESS) {
      mat->Get(AI_MATKEY_COLOR_DIFFUSE, baseColor);
    }
    out.baseColorFactor = Vec4(baseColor.r, baseColor.g, baseColor.b, baseColor.a);

    float metallic = out.metallicFactor;
    mat->Get(AI_MATKEY_METALLIC_FACTOR, metallic);
    out.metallicFactor = metallic;

    float roughness = out.roughnessFactor;
    mat->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness);
    out.roughnessFactor = roughness;

    float shininess = 0.0F;
    if (mat->Get(AI_MATKEY_SHININESS, shininess) == aiReturn_SUCCESS) {
      out.legacyShininess = std::max(0.0F, shininess);
    }

    aiColor3D emissive(0.0F, 0.0F, 0.0F);
    mat->Get(AI_MATKEY_COLOR_EMISSIVE, emissive);
    out.emissiveFactor = Vec3(emissive.r, emissive.g, emissive.b);
    float opacity = 1.0F;
    if (mat->Get(AI_MATKEY_OPACITY, opacity) == aiReturn_SUCCESS) {
      out.baseColorFactor.a *= opacity;
    }

    const std::string baseColorTex =
        GetFirstTexturePath(mat, {aiTextureType_BASE_COLOR, aiTextureType_DIFFUSE, aiTextureType_UNKNOWN});
    const std::string metallicTexPath = GetFirstTexturePath(mat, {aiTextureType_METALNESS});
    const std::string roughnessTexPath = GetFirstTexturePath(mat, {aiTextureType_DIFFUSE_ROUGHNESS});
    const std::string normalTex = GetFirstTexturePath(mat, {aiTextureType_NORMALS, aiTextureType_HEIGHT});
    const std::string occlusionTex = GetFirstTexturePath(mat, {aiTextureType_AMBIENT_OCCLUSION, aiTextureType_LIGHTMAP});
    const std::string emissiveTex = GetFirstTexturePath(mat, {aiTextureType_EMISSIVE});
    const std::string specularTex = GetFirstTexturePath(mat, {aiTextureType_SPECULAR, aiTextureType_SHININESS});

    out.baseColorTex = GetOrCreateTexture(ctx, baseColorTex, true, kDefaultWhiteSrgb);
    out.normalTex = GetOrCreateTexture(ctx, normalTex, false, kDefaultNormal);
    out.occlusionTex = GetOrCreateTexture(ctx, occlusionTex, false, kDefaultWhiteLinear);
    out.emissiveTex = GetOrCreateTexture(ctx, emissiveTex, true, kDefaultBlackLinear);
    out.specularTex = GetOrCreateTexture(ctx, specularTex, true, kDefaultSpecular);

    const std::string metallicTexNorm = NormalizeTextureUri(metallicTexPath);
    const std::string roughnessTexNorm = NormalizeTextureUri(roughnessTexPath);
    const bool hasMetalTex = !metallicTexNorm.empty();
    const bool hasRoughTex = !roughnessTexNorm.empty();
    const bool sameMrTexture = hasMetalTex && hasRoughTex && metallicTexNorm == roughnessTexNorm;

    out.useSeparateMetalRoughness = (hasMetalTex != hasRoughTex) || (hasMetalTex && hasRoughTex && !sameMrTexture);
    out.metallicRoughnessTex = kDefaultWhiteLinear;
    out.metallicTex = kDefaultBlackLinear;
    out.roughnessTex = kDefaultWhiteLinear;
    if (sameMrTexture) {
      out.metallicRoughnessTex = GetOrCreateTexture(ctx, metallicTexPath, false, kDefaultWhiteLinear);
    } else if (out.useSeparateMetalRoughness) {
      if (hasMetalTex) {
        out.metallicTex = GetOrCreateTexture(ctx, metallicTexPath, false, kDefaultBlackLinear);
      }
      if (hasRoughTex) {
        out.roughnessTex = GetOrCreateTexture(ctx, roughnessTexPath, false, kDefaultWhiteLinear);
      }
    } else if (hasMetalTex) {
      out.metallicRoughnessTex = GetOrCreateTexture(ctx, metallicTexPath, false, kDefaultWhiteLinear);
    } else if (hasRoughTex) {
      out.metallicRoughnessTex = GetOrCreateTexture(ctx, roughnessTexPath, false, kDefaultWhiteLinear);
    }

#ifdef AI_MATKEY_GLTF_TEXTURE_SCALE
    float normalScale = 1.0F;
    if (mat->Get(AI_MATKEY_GLTF_TEXTURE_SCALE(aiTextureType_NORMALS, 0), normalScale) == aiReturn_SUCCESS) {
      out.normalScale = normalScale;
    }
#endif
#ifdef AI_MATKEY_GLTF_TEXTURE_STRENGTH
    float occlusionStrength = 1.0F;
    if (mat->Get(AI_MATKEY_GLTF_TEXTURE_STRENGTH(aiTextureType_AMBIENT_OCCLUSION, 0), occlusionStrength) ==
        aiReturn_SUCCESS) {
      out.occlusionStrength = occlusionStrength;
    }
#endif
#ifdef AI_MATKEY_EMISSIVE_INTENSITY
    float emissiveStrength = 1.0F;
    if (mat->Get(AI_MATKEY_EMISSIVE_INTENSITY, emissiveStrength) == aiReturn_SUCCESS) {
      out.emissiveStrength = emissiveStrength;
    }
#endif
#ifdef AI_MATKEY_GLTF_ALPHAMODE
    aiString alphaMode;
    if (mat->Get(AI_MATKEY_GLTF_ALPHAMODE, alphaMode) == aiReturn_SUCCESS) {
      std::string mode = ToLowerAscii(alphaMode.C_Str());
      if (mode == "mask") {
        out.alphaMask = true;
      }
    }
#endif
#ifdef AI_MATKEY_GLTF_ALPHACUTOFF
    float alphaCutoff = out.alphaCutoff;
    if (mat->Get(AI_MATKEY_GLTF_ALPHACUTOFF, alphaCutoff) == aiReturn_SUCCESS) {
      out.alphaCutoff = alphaCutoff;
    }
#endif

    const bool hasSpecWorkflowTexture = !specularTex.empty();
    const bool hasMetalRoughWorkflowTexture = hasMetalTex || hasRoughTex;
    out.useSpecularGlossiness = hasSpecWorkflowTexture && !hasMetalRoughWorkflowTexture;

    if (out.useSpecularGlossiness) {
      // Blinn-Phong shininess to roughness approximation.
      if (out.legacyShininess > 0.0F) {
        out.roughnessFactor = glm::clamp(std::sqrt(2.0F / (out.legacyShininess + 2.0F)), 0.04F, 1.0F);
      } else {
        out.roughnessFactor = 0.7F;
      }
      out.metallicFactor = 0.0F;
      // Most legacy FBX spec-gloss assets (e.g. Mixamo) author normal maps in DirectX convention.
      out.normalGreenInverted = true;
      out.useSeparateMetalRoughness = false;
      out.metallicRoughnessTex = kDefaultWhiteLinear;
      out.metallicTex = kDefaultBlackLinear;
      out.roughnessTex = kDefaultWhiteLinear;
    }

    ctx.dst.materials.push_back(std::move(out));
  }
}

void FinalizeWorldTransforms(Scene& scene) {
  std::function<void(NodeId, const Mat4&)> recurse = [&](NodeId nodeId, const Mat4& parentWorld) {
    Node& node = scene.nodes[nodeId];
    node.worldCurrent = parentWorld * node.localCurrent.ToMat4();
    for (const NodeId child : node.children) {
      recurse(child, node.worldCurrent);
    }
  };

  for (const NodeId root : scene.roots) {
    recurse(root, Mat4(1.0F));
  }
}

void ImportMeshesAndSkeletons(ImportContext& ctx, uint32_t maxBoneInfluence) {
  Skeleton skeleton;
  skeleton.name = "FBXSkeleton";
  std::vector<SkinId> createdSkinIds;
  createdSkinIds.reserve(ctx.src->mNumMeshes);

  const glm::mat3 normalXform = glm::transpose(glm::inverse(glm::mat3(ctx.conv.c)));

  for (unsigned meshIndex = 0; meshIndex < ctx.src->mNumMeshes; ++meshIndex) {
    const aiMesh* srcMesh = ctx.src->mMeshes[meshIndex];

    Mesh dstMesh;
    dstMesh.name = srcMesh->mName.C_Str();
    dstMesh.vertices.resize(srcMesh->mNumVertices);

    std::vector<std::vector<std::pair<uint32_t, float>>> influences(srcMesh->mNumVertices);

    for (unsigned v = 0; v < srcMesh->mNumVertices; ++v) {
      VertexSkinned vvtx;
      vvtx.position = ConvertPosition(ctx.conv, ToVec3(srcMesh->mVertices[v]));

      if (srcMesh->HasNormals()) {
        vvtx.normal = NormalizeSafe(normalXform * ToVec3(srcMesh->mNormals[v]));
      }
      if (srcMesh->HasTangentsAndBitangents()) {
        const Vec3 t = NormalizeSafe(normalXform * ToVec3(srcMesh->mTangents[v]), Vec3(1.0F, 0.0F, 0.0F));
        const Vec3 b = NormalizeSafe(normalXform * ToVec3(srcMesh->mBitangents[v]), Vec3(0.0F, 0.0F, 1.0F));
        const Vec3 n = NormalizeSafe(vvtx.normal, Vec3(0.0F, 1.0F, 0.0F));
        const float handedness = glm::dot(glm::cross(n, t), b) < 0.0F ? -1.0F : 1.0F;
        vvtx.tangent = Vec4(t, handedness);
      }
      if (srcMesh->HasTextureCoords(0)) {
        vvtx.uv0 = Vec2(srcMesh->mTextureCoords[0][v].x, srcMesh->mTextureCoords[0][v].y);
      }
      dstMesh.vertices[v] = vvtx;
    }

    dstMesh.indices.reserve(srcMesh->mNumFaces * 3);
    for (unsigned f = 0; f < srcMesh->mNumFaces; ++f) {
      const aiFace& face = srcMesh->mFaces[f];
      if (face.mNumIndices != 3) {
        continue;
      }
      dstMesh.indices.push_back(face.mIndices[0]);
      dstMesh.indices.push_back(face.mIndices[1]);
      dstMesh.indices.push_back(face.mIndices[2]);
    }

    if (!dstMesh.vertices.empty()) {
      Vec3 minP = dstMesh.vertices[0].position;
      Vec3 maxP = dstMesh.vertices[0].position;
      for (const auto& v : dstMesh.vertices) {
        minP = glm::min(minP, v.position);
        maxP = glm::max(maxP, v.position);
      }
      dstMesh.localBounds = {minP, maxP};
    }

    for (unsigned b = 0; b < srcMesh->mNumBones; ++b) {
      const aiBone* srcBone = srcMesh->mBones[b];
      const std::string boneName = srcBone->mName.C_Str();

      uint32_t boneIndex = 0;
      const auto it = skeleton.boneMap.find(boneName);
      if (it == skeleton.boneMap.end()) {
        Bone bone;
        bone.name = boneName;
        bone.node = kInvalidNodeId;
        const auto nodeIt = ctx.nodeByName.find(boneName);
        if (nodeIt != ctx.nodeByName.end()) {
          bone.node = nodeIt->second;
        }

        const Mat4 srcInvBind = ToMat4(srcBone->mOffsetMatrix);
        bone.inverseBind = ctx.conv.c * srcInvBind * ctx.conv.cInv;
        bone.globalBind = glm::inverse(bone.inverseBind);

        boneIndex = static_cast<uint32_t>(skeleton.bones.size());
        skeleton.boneMap[boneName] = boneIndex;
        skeleton.bones.push_back(std::move(bone));
      } else {
        boneIndex = it->second;
      }

      for (unsigned w = 0; w < srcBone->mNumWeights; ++w) {
        const aiVertexWeight& vw = srcBone->mWeights[w];
        if (vw.mVertexId < influences.size()) {
          influences[vw.mVertexId].push_back({boneIndex, vw.mWeight});
        }
      }
    }

    for (size_t v = 0; v < influences.size(); ++v) {
      auto& inf = influences[v];
      if (inf.empty()) {
        dstMesh.vertices[v].joints = {0, 0, 0, 0};
        dstMesh.vertices[v].weights = {1.0F, 0.0F, 0.0F, 0.0F};
        continue;
      }

      if (inf.size() > maxBoneInfluence) {
        inf.resize(maxBoneInfluence);
      }
      const PackedInfluence4 packed = NormalizeInfluences4(inf);
      dstMesh.vertices[v].joints = packed.joints;
      dstMesh.vertices[v].weights = packed.weights;
    }

    Submesh submesh;
    submesh.firstIndex = 0;
    submesh.indexCount = static_cast<uint32_t>(dstMesh.indices.size());
    if (ctx.src->mNumMaterials > 0) {
      submesh.material = std::min(static_cast<MaterialId>(srcMesh->mMaterialIndex),
                                  static_cast<MaterialId>(ctx.dst.materials.size() - 1));
    }
    dstMesh.submeshes.push_back(submesh);

    const MeshId dstMeshId = static_cast<MeshId>(ctx.dst.meshes.size());
    ctx.dst.meshes.push_back(std::move(dstMesh));

    const auto meshNodeIt = ctx.meshNode.find(meshIndex);
    NodeId assignedNode = kInvalidNodeId;
    if (meshNodeIt != ctx.meshNode.end()) {
      assignedNode = meshNodeIt->second;
      Node& node = ctx.dst.nodes[assignedNode];
      if (!node.mesh.has_value()) {
        node.mesh = dstMeshId;
      } else {
        Node extraNode;
        extraNode.name = node.name + "_mesh_" + std::to_string(dstMeshId);
        extraNode.parent = assignedNode;
        extraNode.localBind = Transform{};
        extraNode.localCurrent = extraNode.localBind;
        extraNode.worldCurrent = node.worldCurrent;

        const NodeId extraId = static_cast<NodeId>(ctx.dst.nodes.size());
        ctx.dst.nodes.push_back(std::move(extraNode));
        ctx.dst.nodes[assignedNode].children.push_back(extraId);
        ctx.dst.nodes[extraId].mesh = dstMeshId;
        assignedNode = extraId;
      }
    }

    if (srcMesh->mNumBones > 0) {
      Skin skin;
      skin.mesh = dstMeshId;
      const SkinId skinId = static_cast<SkinId>(ctx.dst.skins.size());
      ctx.dst.skins.push_back(std::move(skin));
      createdSkinIds.push_back(skinId);
      if (assignedNode != kInvalidNodeId) {
        ctx.dst.nodes[assignedNode].skin = skinId;
      }
    }
  }

  if (!skeleton.bones.empty()) {
    for (size_t i = 0; i < skeleton.bones.size(); ++i) {
      const NodeId nodeId = skeleton.bones[i].node;
      int32_t parentBone = -1;
      NodeId walk = nodeId;
      while (walk != kInvalidNodeId) {
        walk = ctx.dst.nodes[walk].parent;
        if (walk == kInvalidNodeId) {
          break;
        }
        for (size_t b = 0; b < skeleton.bones.size(); ++b) {
          if (skeleton.bones[b].node == walk) {
            parentBone = static_cast<int32_t>(b);
            break;
          }
        }
        if (parentBone >= 0) {
          break;
        }
      }
      skeleton.bones[i].parentBone = parentBone;
      if (parentBone < 0 && skeleton.rootNode == kInvalidNodeId) {
        skeleton.rootNode = nodeId;
      }
    }

    const SkeletonId skeletonId = static_cast<SkeletonId>(ctx.dst.skeletons.size());
    ctx.dst.skeletons.push_back(std::move(skeleton));
    const size_t paletteSize = ctx.dst.skeletons[skeletonId].bones.size();
    for (const SkinId skinId : createdSkinIds) {
      if (skinId >= ctx.dst.skins.size()) {
        continue;
      }
      Skin& skin = ctx.dst.skins[skinId];
      skin.skeleton = skeletonId;
      skin.palette.resize(paletteSize, Mat4(1.0F));
    }
  }
}

void ImportAnimations(ImportContext& ctx) {
  for (unsigned i = 0; i < ctx.src->mNumAnimations; ++i) {
    const aiAnimation* srcAnim = ctx.src->mAnimations[i];

    AnimationClip clip;
    clip.name = srcAnim->mName.C_Str();
    if (clip.name.empty()) {
      clip.name = "Clip_" + std::to_string(i);
    }

    const float ticksPerSec = srcAnim->mTicksPerSecond > 0.0 ? static_cast<float>(srcAnim->mTicksPerSecond) : 30.0F;
    clip.ticksPerSec = ticksPerSec;
    clip.durationSec = srcAnim->mDuration > 0.0 ? static_cast<float>(srcAnim->mDuration / ticksPerSec) : 0.0F;

    for (unsigned c = 0; c < srcAnim->mNumChannels; ++c) {
      const aiNodeAnim* channel = srcAnim->mChannels[c];
      const auto nodeIt = ctx.nodeByName.find(channel->mNodeName.C_Str());
      if (nodeIt == ctx.nodeByName.end()) {
        continue;
      }

      NodeTrack track;
      track.node = nodeIt->second;

      track.posKeys.reserve(channel->mNumPositionKeys);
      for (unsigned k = 0; k < channel->mNumPositionKeys; ++k) {
        KeyVec3 key;
        key.time = static_cast<float>(channel->mPositionKeys[k].mTime / ticksPerSec);
        key.value = ConvertPosition(ctx.conv, ToVec3(channel->mPositionKeys[k].mValue));
        track.posKeys.push_back(key);
      }

      track.rotKeys.reserve(channel->mNumRotationKeys);
      for (unsigned k = 0; k < channel->mNumRotationKeys; ++k) {
        KeyQuat key;
        key.time = static_cast<float>(channel->mRotationKeys[k].mTime / ticksPerSec);
        key.value = ConvertRotation(ctx.conv, ToQuat(channel->mRotationKeys[k].mValue));
        track.rotKeys.push_back(key);
      }

      track.sclKeys.reserve(channel->mNumScalingKeys);
      for (unsigned k = 0; k < channel->mNumScalingKeys; ++k) {
        KeyVec3 key;
        key.time = static_cast<float>(channel->mScalingKeys[k].mTime / ticksPerSec);
        key.value = ToVec3(channel->mScalingKeys[k].mValue);
        track.sclKeys.push_back(key);
      }

      clip.tracks.push_back(std::move(track));
    }

    ctx.dst.clips.push_back(std::move(clip));
  }
}

void ImportLights(ImportContext& ctx) {
  for (unsigned i = 0; i < ctx.src->mNumLights; ++i) {
    const aiLight* srcLight = ctx.src->mLights[i];
    Light light;

    if (srcLight->mType == aiLightSource_DIRECTIONAL) {
      light.type = LightType::kDirectional;
    } else if (srcLight->mType == aiLightSource_POINT) {
      light.type = LightType::kPoint;
    } else if (srcLight->mType == aiLightSource_SPOT) {
      light.type = LightType::kSpot;
    } else {
      continue;
    }

    light.color = Vec3(srcLight->mColorDiffuse.r, srcLight->mColorDiffuse.g, srcLight->mColorDiffuse.b);
    light.intensity = 1.0F;
    light.range = srcLight->mAttenuationLinear > 0.0F ? (1.0F / srcLight->mAttenuationLinear) : 50.0F;
    light.direction = ConvertDirection(ctx.conv, ToVec3(srcLight->mDirection));
    light.innerCone = srcLight->mAngleInnerCone;
    light.outerCone = srcLight->mAngleOuterCone;

    const LightId lightId = static_cast<LightId>(ctx.dst.lights.size());
    ctx.dst.lights.push_back(light);

    const auto nodeIt = ctx.nodeByName.find(srcLight->mName.C_Str());
    if (nodeIt != ctx.nodeByName.end()) {
      ctx.dst.nodes[nodeIt->second].light = lightId;
    }
  }
}

}  // namespace

LoadResult<Scene> AssimpFbxImporter::Import(const std::string& path, const ImportOptions& opt) const {
  Assimp::Importer importer;
  importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);
  importer.SetPropertyInteger(AI_CONFIG_PP_LBW_MAX_WEIGHTS, static_cast<int>(opt.maxBoneInfluence));

  const uint32_t flags = aiProcess_Triangulate |
                         aiProcess_JoinIdenticalVertices |
                         aiProcess_ImproveCacheLocality |
                         aiProcess_GenNormals |
                         aiProcess_CalcTangentSpace |
                         aiProcess_FlipUVs |
                         aiProcess_LimitBoneWeights;

  const aiScene* srcScene = importer.ReadFile(path, flags);
  if (srcScene == nullptr || srcScene->mRootNode == nullptr) {
    return LoadResult<Scene>{.value = std::nullopt, .error = importer.GetErrorString()};
  }

  ImportContext ctx;
  ctx.src = srcScene;
  ctx.conv = BuildConversion(srcScene, opt);
  ctx.sourceDir = std::filesystem::absolute(std::filesystem::path(path)).parent_path();

  AddDefaultTexture(ctx, "__default_white__");
  AddDefaultTexture(ctx, "__default_black__");
  AddDefaultTexture(ctx, "__default_normal__");
  AddDefaultTexture(ctx, "__default_specular__");
  AddDefaultTexture(ctx, "__default_linear_white__");

  BuildNodesRecursive(ctx, srcScene->mRootNode, kInvalidNodeId);
  ImportMaterials(ctx);
  ImportMeshesAndSkeletons(ctx, opt.maxBoneInfluence);
  ImportAnimations(ctx);
  ImportLights(ctx);
  FinalizeWorldTransforms(ctx.dst);

  return LoadResult<Scene>{.value = std::move(ctx.dst), .error = {}};
}

}  // namespace vv
