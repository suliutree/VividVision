#pragma once

#include <vector>

#include "render/scene/SceneTypes.hpp"

namespace vv {

struct AnimatorState {
  ClipId clip = 0;
  float timeSec = 0.0F;
  float speed = 1.0F;
  bool loop = true;
  bool paused = false;
};

class Animator {
 public:
  Animator() = default;

  void Bind(const Scene* scene, SkeletonId skeletonId);
  void SetClip(ClipId id, bool loop);
  void SetPaused(bool paused);
  void SetSpeed(float speed);
  void Update(float dtSec);

  [[nodiscard]] const AnimatorState& State() const { return state_; }
  [[nodiscard]] const std::vector<Mat4>& Palette() const { return palette_; }

 private:
  [[nodiscard]] Transform SampleNodeTransform(const AnimationClip& clip, NodeId nodeId, float timeSec) const;

  const Scene* scene_ = nullptr;
  SkeletonId skeletonId_ = 0;
  AnimatorState state_{};
  std::vector<Mat4> palette_;
};

}  // namespace vv
