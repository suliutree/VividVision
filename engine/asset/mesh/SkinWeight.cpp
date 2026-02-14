#include "asset/mesh/SkinWeight.hpp"

#include <algorithm>

namespace vv {

PackedInfluence4 NormalizeInfluences4(std::vector<std::pair<uint32_t, float>> influences) {
  PackedInfluence4 packed;

  if (influences.empty()) {
    return packed;
  }

  std::sort(influences.begin(), influences.end(), [](const auto& a, const auto& b) {
    return a.second > b.second;
  });

  if (influences.size() > kMaxBoneInfluence) {
    influences.resize(kMaxBoneInfluence);
  }

  float sum = 0.0F;
  for (const auto& entry : influences) {
    sum += entry.second;
  }
  if (sum <= 1e-8F) {
    return packed;
  }

  packed.weights = {0.0F, 0.0F, 0.0F, 0.0F};
  for (size_t i = 0; i < influences.size(); ++i) {
    packed.joints[i] = static_cast<uint16_t>(influences[i].first);
    packed.weights[i] = influences[i].second / sum;
  }
  return packed;
}

}  // namespace vv
