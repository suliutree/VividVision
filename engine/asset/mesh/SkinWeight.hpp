#pragma once

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

#include "core/types/CommonTypes.hpp"

namespace vv {

struct PackedInfluence4 {
  std::array<uint16_t, kMaxBoneInfluence> joints{0, 0, 0, 0};
  std::array<float, kMaxBoneInfluence> weights{1.0F, 0.0F, 0.0F, 0.0F};
};

PackedInfluence4 NormalizeInfluences4(std::vector<std::pair<uint32_t, float>> influences);

}  // namespace vv
