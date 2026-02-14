#include <cassert>
#include <cmath>

#include "render/animation/Animator.hpp"

int main() {
  vv::Scene scene;
  scene.nodes.resize(1);
  scene.nodes[0].name = "RootBone";
  scene.nodes[0].localBind.translation = vv::Vec3(0.0F);

  vv::Skeleton skeleton;
  skeleton.name = "TestSkeleton";
  skeleton.rootNode = 0;
  vv::Bone bone;
  bone.name = "RootBone";
  bone.node = 0;
  bone.parentBone = -1;
  bone.inverseBind = vv::Mat4(1.0F);
  skeleton.bones.push_back(bone);
  skeleton.boneMap[bone.name] = 0;
  scene.skeletons.push_back(skeleton);

  vv::AnimationClip clip;
  clip.name = "MoveX";
  clip.durationSec = 1.0F;
  clip.ticksPerSec = 30.0F;

  vv::NodeTrack track;
  track.node = 0;
  track.posKeys.push_back(vv::KeyVec3{.time = 0.0F, .value = vv::Vec3(0.0F, 0.0F, 0.0F)});
  track.posKeys.push_back(vv::KeyVec3{.time = 1.0F, .value = vv::Vec3(1.0F, 0.0F, 0.0F)});
  track.rotKeys.push_back(vv::KeyQuat{.time = 0.0F, .value = vv::Quat(1.0F, 0.0F, 0.0F, 0.0F)});
  track.sclKeys.push_back(vv::KeyVec3{.time = 0.0F, .value = vv::Vec3(1.0F, 1.0F, 1.0F)});
  clip.tracks.push_back(track);
  scene.clips.push_back(clip);

  vv::Animator animator;
  animator.Bind(&scene, 0);
  animator.SetClip(0, true);
  animator.Update(0.5F);

  const auto& palette = animator.Palette();
  assert(!palette.empty());
  const float x = palette[0][3][0];
  assert(std::fabs(x - 0.5F) < 1e-3F);

  return 0;
}
