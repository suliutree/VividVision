#pragma once

#include <vulkan/vulkan.h>

namespace vv {

class VulkanAllocator {
 public:
  void Initialize(VkInstance, VkPhysicalDevice, VkDevice) {}
  void Shutdown() {}
};

}  // namespace vv
