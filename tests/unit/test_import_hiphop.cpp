#include <cassert>
#include <cmath>

#include "asset/import/AssimpFbxImporter.hpp"
#include "render/scene/SceneTypes.hpp"

int main() {
  vv::AssimpFbxImporter importer;
  vv::ImportOptions options;
  const auto loaded = importer.Import("assets/fbx/Hip_Hop_Dancing.fbx", options);
  assert(loaded.Ok());

  const vv::Scene& scene = *loaded.value;
  const vv::SceneStats stats = vv::ComputeSceneStats(scene);
  assert(stats.meshCount == 1);
  assert(stats.materialCount == 1);
  assert(stats.textureCount >= 7);
  assert(stats.skeletonCount == 1);
  assert(stats.boneCount >= 60);
  assert(stats.clipCount >= 1);

  const vv::Material& mat = scene.materials[0];
  assert(mat.useSpecularGlossiness);
  assert(mat.normalGreenInverted);
  assert(!mat.useSeparateMetalRoughness);
  assert(mat.normalScale > 0.0F);
  assert(mat.occlusionStrength >= 0.0F);
  assert(mat.emissiveStrength >= 0.0F);
  assert(mat.baseColorTex < scene.textures.size());
  assert(mat.occlusionTex < scene.textures.size());
  assert(mat.normalTex < scene.textures.size());
  assert(mat.specularTex < scene.textures.size());
  assert(mat.metallicFactor >= 0.0F && mat.metallicFactor <= 0.05F);
  assert(mat.roughnessFactor > 0.2F && mat.roughnessFactor < 0.5F);

  const vv::Texture& base = scene.textures[mat.baseColorTex];
  const vv::Texture& normal = scene.textures[mat.normalTex];
  const vv::Texture& spec = scene.textures[mat.specularTex];
  assert(base.width > 0 && base.height > 0 && !base.pixels.empty());
  assert(normal.width == base.width && normal.height == base.height);
  assert(spec.width == base.width && spec.height == base.height);

  return 0;
}
