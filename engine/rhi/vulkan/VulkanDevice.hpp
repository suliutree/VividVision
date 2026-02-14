#pragma once

#include <cstdint>
#include <vector>

#include <vulkan/vulkan.h>

namespace vv {

class VulkanDevice {
 public:
  VulkanDevice() = default;
  ~VulkanDevice();

  VulkanDevice(VulkanDevice&& other) noexcept;
  VulkanDevice& operator=(VulkanDevice&& other) noexcept;

  VulkanDevice(const VulkanDevice&) = delete;
  VulkanDevice& operator=(const VulkanDevice&) = delete;

  static VulkanDevice Create(VkInstance instance, VkSurfaceKHR surface);

  [[nodiscard]] VkPhysicalDevice PhysicalDevice() const noexcept { return physicalDevice_; }
  [[nodiscard]] VkDevice Get() const noexcept { return device_; }
  [[nodiscard]] VkQueue GraphicsQueue() const noexcept { return graphicsQueue_; }
  [[nodiscard]] uint32_t GraphicsQueueFamily() const noexcept { return graphicsQueueFamily_; }

 private:
  VulkanDevice(VkPhysicalDevice physicalDevice, VkDevice device, uint32_t graphicsQueueFamily, VkQueue graphicsQueue);

  VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;
  uint32_t graphicsQueueFamily_ = 0;
  VkQueue graphicsQueue_ = VK_NULL_HANDLE;
};

}  // namespace vv
