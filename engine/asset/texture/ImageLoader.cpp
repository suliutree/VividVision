#include "asset/texture/ImageLoader.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <cstring>

namespace vv {

namespace {

std::optional<ImageRgba8> CopyDecodedImage(stbi_uc* data, int width, int height) {
  if (data == nullptr || width <= 0 || height <= 0) {
    return std::nullopt;
  }

  ImageRgba8 out;
  out.width = static_cast<uint32_t>(width);
  out.height = static_cast<uint32_t>(height);
  out.pixels.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
  std::memcpy(out.pixels.data(), data, out.pixels.size());
  stbi_image_free(data);
  return out;
}

}  // namespace

std::optional<ImageRgba8> LoadImageRgba8(const std::string& absolutePath) {
  int width = 0;
  int height = 0;
  int channels = 0;

  stbi_uc* data = stbi_load(absolutePath.c_str(), &width, &height, &channels, STBI_rgb_alpha);
  return CopyDecodedImage(data, width, height);
}

std::optional<ImageRgba8> LoadImageRgba8FromMemory(const uint8_t* bytes, size_t sizeBytes) {
  if (bytes == nullptr || sizeBytes == 0) {
    return std::nullopt;
  }

  int width = 0;
  int height = 0;
  int channels = 0;
  stbi_uc* data = stbi_load_from_memory(bytes,
                                        static_cast<int>(sizeBytes),
                                        &width,
                                        &height,
                                        &channels,
                                        STBI_rgb_alpha);
  return CopyDecodedImage(data, width, height);
}

}  // namespace vv
