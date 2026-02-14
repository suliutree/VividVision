#pragma once

#include <array>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/math/MathTypes.hpp"
#include "core/types/CommonTypes.hpp"

namespace vv {

enum class PixelFormat {
  kUnknown,
  kR8G8B8A8,
  kR8G8B8A8_SRGB,
};

struct AABB {
  Vec3 min{0.0F};
  Vec3 max{0.0F};
};

struct VertexSkinned {
  Vec3 position{0.0F};
  Vec3 normal{0.0F, 1.0F, 0.0F};
  Vec4 tangent{1.0F, 0.0F, 0.0F, 1.0F};
  Vec2 uv0{0.0F};
  std::array<uint16_t, kMaxBoneInfluence> joints{0, 0, 0, 0};
  std::array<float, kMaxBoneInfluence> weights{1.0F, 0.0F, 0.0F, 0.0F};
};

struct Submesh {
  uint32_t firstIndex = 0;
  uint32_t indexCount = 0;
  MaterialId material = 0;
};

struct Mesh {
  std::string name;
  std::vector<VertexSkinned> vertices;
  std::vector<uint32_t> indices;
  std::vector<Submesh> submeshes;
  AABB localBounds;
};

struct Bone {
  std::string name;
  NodeId node = kInvalidNodeId;
  int32_t parentBone = -1;
  Mat4 inverseBind{1.0F};
  Mat4 globalBind{1.0F};
};

struct Skeleton {
  std::string name;
  NodeId rootNode = kInvalidNodeId;
  std::vector<Bone> bones;
  std::unordered_map<std::string, uint32_t> boneMap;
};

struct Skin {
  SkeletonId skeleton = 0;
  MeshId mesh = 0;
  std::vector<Mat4> palette;
};

struct KeyVec3 {
  float time = 0.0F;
  Vec3 value{0.0F};
};

struct KeyQuat {
  float time = 0.0F;
  Quat value{1.0F, 0.0F, 0.0F, 0.0F};
};

struct NodeTrack {
  NodeId node = kInvalidNodeId;
  std::vector<KeyVec3> posKeys;
  std::vector<KeyQuat> rotKeys;
  std::vector<KeyVec3> sclKeys;
};

struct AnimationClip {
  std::string name;
  float durationSec = 0.0F;
  float ticksPerSec = 30.0F;
  std::vector<NodeTrack> tracks;
};

struct Texture {
  std::string uri;
  uint32_t width = 0;
  uint32_t height = 0;
  PixelFormat format = PixelFormat::kUnknown;
  bool srgb = false;
  std::vector<uint8_t> pixels;
};

struct Material {
  std::string name;
  Vec4 baseColorFactor{1.0F};
  float metallicFactor = 0.0F;
  float roughnessFactor = 1.0F;
  Vec3 emissiveFactor{0.0F};
  float emissiveStrength = 1.0F;
  float normalScale = 1.0F;
  float occlusionStrength = 1.0F;
  TextureId baseColorTex = 0;
  TextureId metallicRoughnessTex = 0;
  TextureId metallicTex = 0;
  TextureId roughnessTex = 0;
  TextureId normalTex = 0;
  TextureId occlusionTex = 0;
  TextureId emissiveTex = 0;
  TextureId specularTex = 0;
  bool useSeparateMetalRoughness = false;
  bool useSpecularGlossiness = false;
  bool normalGreenInverted = false;
  bool gridOverlay = false;
  float legacyShininess = 0.0F;
  bool alphaMask = false;
  float alphaCutoff = 0.5F;
};

enum class LightType {
  kDirectional,
  kPoint,
  kSpot,
};

struct Light {
  LightType type = LightType::kDirectional;
  Vec3 color{1.0F};
  float intensity = 1.0F;
  float range = 50.0F;
  Vec3 direction{0.0F, -1.0F, 0.0F};
  float innerCone = 0.3F;
  float outerCone = 0.5F;
};

struct Node {
  std::string name;
  NodeId parent = kInvalidNodeId;
  std::vector<NodeId> children;
  Transform localBind;
  Transform localCurrent;
  Mat4 worldCurrent{1.0F};
  std::optional<MeshId> mesh;
  std::optional<SkinId> skin;
  std::optional<LightId> light;
};

struct Scene {
  std::vector<NodeId> roots;
  std::vector<Node> nodes;
  std::vector<Mesh> meshes;
  std::vector<Skeleton> skeletons;
  std::vector<Skin> skins;
  std::vector<AnimationClip> clips;
  std::vector<Material> materials;
  std::vector<Texture> textures;
  std::vector<Light> lights;
};

struct SceneStats {
  size_t meshCount = 0;
  size_t materialCount = 0;
  size_t textureCount = 0;
  size_t skeletonCount = 0;
  size_t boneCount = 0;
  size_t clipCount = 0;
  size_t lightCount = 0;
  uint64_t triangleCount = 0;
};

inline SceneStats ComputeSceneStats(const Scene& scene) {
  SceneStats stats;
  stats.meshCount = scene.meshes.size();
  stats.materialCount = scene.materials.size();
  stats.textureCount = scene.textures.size();
  stats.skeletonCount = scene.skeletons.size();
  stats.clipCount = scene.clips.size();
  stats.lightCount = scene.lights.size();

  for (const auto& mesh : scene.meshes) {
    stats.triangleCount += mesh.indices.size() / 3;
  }

  for (const auto& skeleton : scene.skeletons) {
    stats.boneCount += skeleton.bones.size();
  }

  return stats;
}

}  // namespace vv
