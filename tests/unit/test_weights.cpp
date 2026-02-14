#include <cassert>
#include <cmath>
#include <utility>
#include <vector>

#include "asset/mesh/SkinWeight.hpp"

int main() {
  std::vector<std::pair<uint32_t, float>> influences = {
      {3, 0.1F}, {7, 0.7F}, {2, 0.15F}, {6, 0.04F}, {9, 0.01F},
  };

  const vv::PackedInfluence4 packed = vv::NormalizeInfluences4(influences);

  const float sum = packed.weights[0] + packed.weights[1] + packed.weights[2] + packed.weights[3];
  assert(std::fabs(sum - 1.0F) < 1e-5F);
  assert(packed.joints[0] == 7);
  assert(packed.joints[1] == 2);
  assert(packed.joints[2] == 3);
  assert(packed.joints[3] == 6);

  return 0;
}
