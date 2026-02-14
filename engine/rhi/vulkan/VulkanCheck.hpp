#pragma once

#include <stdexcept>
#include <string>

#include <vulkan/vulkan.h>

namespace vv {

inline void VkCheck(VkResult result, const char* message) {
  if (result != VK_SUCCESS) {
    throw std::runtime_error(std::string(message) + " (VkResult=" + std::to_string(static_cast<int>(result)) + ")");
  }
}

}  // namespace vv
