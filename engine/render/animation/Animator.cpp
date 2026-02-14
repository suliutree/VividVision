#include "render/animation/Animator.hpp"

#include <algorithm>
#include <cmath>

namespace vv {
namespace {

float WrapTime(float timeSec, float durationSec, bool loop) {
  if (durationSec <= 0.0F) {
    return 0.0F;
  }
  if (!loop) {
    return std::clamp(timeSec, 0.0F, durationSec);
  }
  float wrapped = std::fmod(timeSec, durationSec);
  if (wrapped < 0.0F) {
    wrapped += durationSec;
  }
  return wrapped;
}

const NodeTrack* FindTrack(const AnimationClip& clip, NodeId nodeId) {
  for (const auto& track : clip.tracks) {
    if (track.node == nodeId) {
      return &track;
    }
  }
  return nullptr;
}

Vec3 SampleVec3(const std::vector<KeyVec3>& keys, float t, const Vec3& fallback) {
  if (keys.empty()) {
    return fallback;
  }
  if (keys.size() == 1 || t <= keys.front().time) {
    return keys.front().value;
  }
  if (t >= keys.back().time) {
    return keys.back().value;
  }

  for (size_t i = 0; i + 1 < keys.size(); ++i) {
    if (t >= keys[i].time && t <= keys[i + 1].time) {
      const float interval = keys[i + 1].time - keys[i].time;
      const float alpha = interval > 0.0F ? (t - keys[i].time) / interval : 0.0F;
      return glm::mix(keys[i].value, keys[i + 1].value, alpha);
    }
  }
  return keys.back().value;
}

Quat SampleQuat(const std::vector<KeyQuat>& keys, float t, const Quat& fallback) {
  if (keys.empty()) {
    return fallback;
  }
  if (keys.size() == 1 || t <= keys.front().time) {
    return glm::normalize(keys.front().value);
  }
  if (t >= keys.back().time) {
    return glm::normalize(keys.back().value);
  }

  for (size_t i = 0; i + 1 < keys.size(); ++i) {
    if (t >= keys[i].time && t <= keys[i + 1].time) {
      const float interval = keys[i + 1].time - keys[i].time;
      const float alpha = interval > 0.0F ? (t - keys[i].time) / interval : 0.0F;
      return glm::normalize(glm::slerp(keys[i].value, keys[i + 1].value, alpha));
    }
  }
  return glm::normalize(keys.back().value);
}

}  // namespace

void Animator::Bind(const Scene* scene, SkeletonId skeletonId) {
  scene_ = scene;
  skeletonId_ = skeletonId;
  palette_.clear();
  if (scene_ != nullptr && skeletonId_ < scene_->skeletons.size()) {
    palette_.resize(scene_->skeletons[skeletonId_].bones.size(), Mat4(1.0F));
  }
}

void Animator::SetClip(ClipId id, bool loop) {
  state_.clip = id;
  state_.loop = loop;
  state_.timeSec = 0.0F;
}

void Animator::SetPaused(bool paused) {
  state_.paused = paused;
}

void Animator::SetSpeed(float speed) {
  state_.speed = speed;
}

Transform Animator::SampleNodeTransform(const AnimationClip& clip, NodeId nodeId, float timeSec) const {
  const auto& node = scene_->nodes[nodeId];
  Transform sampled = node.localBind;

  const NodeTrack* track = FindTrack(clip, nodeId);
  if (track == nullptr) {
    return sampled;
  }

  sampled.translation = SampleVec3(track->posKeys, timeSec, sampled.translation);
  sampled.rotation = SampleQuat(track->rotKeys, timeSec, sampled.rotation);
  sampled.scale = SampleVec3(track->sclKeys, timeSec, sampled.scale);
  return sampled;
}

void Animator::Update(float dtSec) {
  if (scene_ == nullptr || skeletonId_ >= scene_->skeletons.size()) {
    return;
  }
  if (scene_->clips.empty() || state_.clip >= scene_->clips.size()) {
    return;
  }

  const AnimationClip& clip = scene_->clips[state_.clip];
  if (!state_.paused) {
    state_.timeSec += dtSec * state_.speed;
  }

  const float sampleTime = WrapTime(state_.timeSec, clip.durationSec, state_.loop);

  const Skeleton& skeleton = scene_->skeletons[skeletonId_];
  std::vector<Mat4> local(skeleton.bones.size(), Mat4(1.0F));
  std::vector<Mat4> global(skeleton.bones.size(), Mat4(1.0F));

  for (size_t i = 0; i < skeleton.bones.size(); ++i) {
    const NodeId nodeId = skeleton.bones[i].node;
    local[i] = SampleNodeTransform(clip, nodeId, sampleTime).ToMat4();
  }

  for (size_t i = 0; i < skeleton.bones.size(); ++i) {
    const int32_t parent = skeleton.bones[i].parentBone;
    if (parent < 0) {
      global[i] = local[i];
    } else {
      global[i] = global[static_cast<size_t>(parent)] * local[i];
    }
    palette_[i] = global[i] * skeleton.bones[i].inverseBind;
  }
}

}  // namespace vv
