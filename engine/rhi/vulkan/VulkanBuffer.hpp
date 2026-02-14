#pragma once

#include <cstdint>

#include <vulkan/vulkan.h>

namespace vv {

struct VulkanBuffer {
  VkBuffer buffer = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  uint64_t size = 0;
};

}  // namespace vv
