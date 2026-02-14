#include "rhi/vulkan/VulkanSurface.hpp"

#include <utility>

namespace vv {

VulkanSurface::VulkanSurface(VkInstance instance, VkSurfaceKHR surface)
    : instance_(instance), surface_(surface) {}

VulkanSurface::~VulkanSurface() {
  if (instance_ != VK_NULL_HANDLE && surface_ != VK_NULL_HANDLE) {
    vkDestroySurfaceKHR(instance_, surface_, nullptr);
    surface_ = VK_NULL_HANDLE;
  }
}

VulkanSurface::VulkanSurface(VulkanSurface&& other) noexcept {
  std::swap(instance_, other.instance_);
  std::swap(surface_, other.surface_);
}

VulkanSurface& VulkanSurface::operator=(VulkanSurface&& other) noexcept {
  if (this != &other) {
    if (instance_ != VK_NULL_HANDLE && surface_ != VK_NULL_HANDLE) {
      vkDestroySurfaceKHR(instance_, surface_, nullptr);
    }
    instance_ = other.instance_;
    surface_ = other.surface_;
    other.instance_ = VK_NULL_HANDLE;
    other.surface_ = VK_NULL_HANDLE;
  }
  return *this;
}

}  // namespace vv
