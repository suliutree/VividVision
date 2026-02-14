#pragma once

#include <vulkan/vulkan.h>

namespace vv {

struct VulkanImage {
  VkImage image = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
};

}  // namespace vv
