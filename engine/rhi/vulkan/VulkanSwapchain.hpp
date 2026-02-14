#pragma once

#include <vector>

#include <vulkan/vulkan.h>

#include "core/types/CommonTypes.hpp"

namespace vv {

struct AcquireResult {
  VkResult result = VK_SUCCESS;
  uint32_t imageIndex = 0;
};

class VulkanSwapchain {
 public:
  VulkanSwapchain() = default;
  ~VulkanSwapchain();

  VulkanSwapchain(VulkanSwapchain&& other) noexcept;
  VulkanSwapchain& operator=(VulkanSwapchain&& other) noexcept;

  VulkanSwapchain(const VulkanSwapchain&) = delete;
  VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;

  static VulkanSwapchain Create(VkPhysicalDevice physicalDevice,
                                VkDevice device,
                                VkSurfaceKHR surface,
                                uint32_t queueFamily,
                                Extent2D windowExtent);

  void Recreate(VkPhysicalDevice physicalDevice,
                VkDevice device,
                VkSurfaceKHR surface,
                uint32_t queueFamily,
                Extent2D windowExtent);

  AcquireResult AcquireNextImage(VkDevice device, VkSemaphore imageAvailable);
  VkResult Present(VkQueue queue, uint32_t imageIndex, VkSemaphore renderFinished) const;

  [[nodiscard]] VkFormat ImageFormat() const noexcept { return imageFormat_; }
  [[nodiscard]] VkExtent2D Extent() const noexcept { return extent_; }
  [[nodiscard]] const std::vector<VkImageView>& ImageViews() const noexcept { return imageViews_; }
  [[nodiscard]] VkSwapchainKHR Get() const noexcept { return swapchain_; }

 private:
  void Cleanup(VkDevice device);

  VkDevice device_ = VK_NULL_HANDLE;
  VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
  VkFormat imageFormat_ = VK_FORMAT_B8G8R8A8_UNORM;
  VkExtent2D extent_{};
  std::vector<VkImage> images_;
  std::vector<VkImageView> imageViews_;
};

}  // namespace vv
