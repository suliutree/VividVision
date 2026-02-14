#pragma once

#include <vulkan/vulkan.h>

namespace vv {

class VulkanCommand {
 public:
  void Initialize(VkDevice) {}
  void Shutdown(VkDevice) {}
};

}  // namespace vv
