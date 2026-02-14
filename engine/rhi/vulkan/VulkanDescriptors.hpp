#pragma once

#include <vulkan/vulkan.h>

namespace vv {

class VulkanDescriptors {
 public:
  void Initialize(VkDevice) {}
  void Shutdown(VkDevice) {}
};

}  // namespace vv
