#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace vv {

struct ImageRgba8 {
  uint32_t width = 0;
  uint32_t height = 0;
  std::vector<uint8_t> pixels;
};

std::optional<ImageRgba8> LoadImageRgba8(const std::string& absolutePath);
std::optional<ImageRgba8> LoadImageRgba8FromMemory(const uint8_t* bytes, size_t sizeBytes);

}  // namespace vv
