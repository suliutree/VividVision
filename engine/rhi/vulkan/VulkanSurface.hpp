#pragma once

#include <vulkan/vulkan.h>

namespace vv {

class VulkanSurface {
 public:
  VulkanSurface() = default;
  VulkanSurface(VkInstance instance, VkSurfaceKHR surface);
  ~VulkanSurface();

  VulkanSurface(VulkanSurface&& other) noexcept;
  VulkanSurface& operator=(VulkanSurface&& other) noexcept;

  VulkanSurface(const VulkanSurface&) = delete;
  VulkanSurface& operator=(const VulkanSurface&) = delete;

  [[nodiscard]] VkSurfaceKHR Get() const noexcept { return surface_; }

 private:
  VkInstance instance_ = VK_NULL_HANDLE;
  VkSurfaceKHR surface_ = VK_NULL_HANDLE;
};

}  // namespace vv
