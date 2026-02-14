#pragma once

#include <vector>

#include <vulkan/vulkan.h>

namespace vv {

struct InstanceDesc {
  std::vector<const char*> requiredExtensions;
  bool enableValidation = false;
};

class VulkanInstance {
 public:
  VulkanInstance() = default;
  ~VulkanInstance();

  VulkanInstance(VulkanInstance&& other) noexcept;
  VulkanInstance& operator=(VulkanInstance&& other) noexcept;

  VulkanInstance(const VulkanInstance&) = delete;
  VulkanInstance& operator=(const VulkanInstance&) = delete;

  static VulkanInstance Create(const InstanceDesc& desc);

  [[nodiscard]] VkInstance Get() const noexcept { return instance_; }

 private:
  explicit VulkanInstance(VkInstance instance) : instance_(instance) {}

  VkInstance instance_ = VK_NULL_HANDLE;
};

}  // namespace vv
