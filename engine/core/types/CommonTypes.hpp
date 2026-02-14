#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace vv {

using NodeId = uint32_t;
using MeshId = uint32_t;
using SkinId = uint32_t;
using SkeletonId = uint32_t;
using ClipId = uint32_t;
using MaterialId = uint32_t;
using TextureId = uint32_t;
using LightId = uint32_t;

constexpr NodeId kInvalidNodeId = UINT32_MAX;
constexpr uint32_t kMaxBoneInfluence = 4;

struct Extent2D {
  uint32_t width = 0;
  uint32_t height = 0;
};

struct FileReadResult {
  bool ok = false;
  std::string error;
  std::string bytes;
};

template <typename T>
struct LoadResult {
  std::optional<T> value;
  std::string error;

  [[nodiscard]] bool Ok() const { return value.has_value(); }
};

}  // namespace vv
