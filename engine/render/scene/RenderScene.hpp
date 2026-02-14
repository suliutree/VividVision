#pragma once

#include <vector>

#include "core/math/MathTypes.hpp"
#include "render/scene/SceneTypes.hpp"

namespace vv {

struct FrameContext {
  Mat4 view{1.0F};
  Mat4 proj{1.0F};
  Vec3 cameraPos{0.0F, 0.0F, 3.0F};
  float deltaSec = 0.0F;
  float enableNormalMap = 1.0F;
  float enableSpecularIbl = 1.0F;
};

struct RenderScene {
  const Scene* scene = nullptr;
  const std::vector<Mat4>* skinPalette = nullptr;
  const std::vector<uint32_t>* skeletonPaletteOffsets = nullptr;  // index by SkeletonId
};

}  // namespace vv
