#include "rhi/vulkan/VulkanDevice.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>
#include <utility>
#include <vector>

#include "rhi/vulkan/VulkanCheck.hpp"

namespace vv {
namespace {

struct Candidate {
  VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
  uint32_t graphicsQueueFamily = UINT32_MAX;
  bool supportsSwapchain = false;
};

std::vector<VkPhysicalDevice> EnumeratePhysicalDevices(VkInstance instance) {
  uint32_t count = 0;
  VkCheck(vkEnumeratePhysicalDevices(instance, &count, nullptr), "vkEnumeratePhysicalDevices count failed");
  std::vector<VkPhysicalDevice> devices(count);
  VkCheck(vkEnumeratePhysicalDevices(instance, &count, devices.data()), "vkEnumeratePhysicalDevices failed");
  return devices;
}

std::vector<VkQueueFamilyProperties> GetQueueFamilies(VkPhysicalDevice physicalDevice) {
  uint32_t count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, nullptr);
  std::vector<VkQueueFamilyProperties> families(count);
  vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, families.data());
  return families;
}

std::vector<VkExtensionProperties> GetDeviceExtensions(VkPhysicalDevice physicalDevice) {
  uint32_t count = 0;
  VkCheck(vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, nullptr),
          "vkEnumerateDeviceExtensionProperties count failed");
  std::vector<VkExtensionProperties> extensions(count);
  VkCheck(vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, extensions.data()),
          "vkEnumerateDeviceExtensionProperties failed");
  return extensions;
}

bool HasDeviceExtension(const std::vector<VkExtensionProperties>& available, const char* extensionName) {
  return std::any_of(available.begin(), available.end(), [extensionName](const VkExtensionProperties& extension) {
    return std::strcmp(extension.extensionName, extensionName) == 0;
  });
}

Candidate EvaluateDevice(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface) {
  Candidate candidate;
  candidate.physicalDevice = physicalDevice;

  const auto queueFamilies = GetQueueFamilies(physicalDevice);
  for (uint32_t i = 0; i < queueFamilies.size(); ++i) {
    const bool graphics = (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
    VkBool32 present = VK_FALSE;
    VkCheck(vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, surface, &present), "vkGetPhysicalDeviceSurfaceSupportKHR failed");
    if (graphics && present == VK_TRUE) {
      candidate.graphicsQueueFamily = i;
      break;
    }
  }

  if (candidate.graphicsQueueFamily == UINT32_MAX) {
    return candidate;
  }

  const auto extensions = GetDeviceExtensions(physicalDevice);
  candidate.supportsSwapchain = HasDeviceExtension(extensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME);

  return candidate;
}

}  // namespace

VulkanDevice::VulkanDevice(VkPhysicalDevice physicalDevice, VkDevice device, uint32_t graphicsQueueFamily, VkQueue graphicsQueue)
    : physicalDevice_(physicalDevice),
      device_(device),
      graphicsQueueFamily_(graphicsQueueFamily),
      graphicsQueue_(graphicsQueue) {}

VulkanDevice::~VulkanDevice() {
  if (device_ != VK_NULL_HANDLE) {
    vkDestroyDevice(device_, nullptr);
    device_ = VK_NULL_HANDLE;
  }
}

VulkanDevice::VulkanDevice(VulkanDevice&& other) noexcept {
  std::swap(physicalDevice_, other.physicalDevice_);
  std::swap(device_, other.device_);
  std::swap(graphicsQueueFamily_, other.graphicsQueueFamily_);
  std::swap(graphicsQueue_, other.graphicsQueue_);
}

VulkanDevice& VulkanDevice::operator=(VulkanDevice&& other) noexcept {
  if (this != &other) {
    if (device_ != VK_NULL_HANDLE) {
      vkDestroyDevice(device_, nullptr);
    }
    physicalDevice_ = other.physicalDevice_;
    device_ = other.device_;
    graphicsQueueFamily_ = other.graphicsQueueFamily_;
    graphicsQueue_ = other.graphicsQueue_;

    other.physicalDevice_ = VK_NULL_HANDLE;
    other.device_ = VK_NULL_HANDLE;
    other.graphicsQueueFamily_ = UINT32_MAX;
    other.graphicsQueue_ = VK_NULL_HANDLE;
  }
  return *this;
}

VulkanDevice VulkanDevice::Create(VkInstance instance, VkSurfaceKHR surface) {
  const auto devices = EnumeratePhysicalDevices(instance);
  if (devices.empty()) {
    throw std::runtime_error("No Vulkan physical devices found");
  }

  Candidate selected;
  for (const VkPhysicalDevice physicalDevice : devices) {
    Candidate candidate = EvaluateDevice(physicalDevice, surface);
    if (candidate.graphicsQueueFamily != UINT32_MAX && candidate.supportsSwapchain) {
      selected = candidate;
      break;
    }
  }

  if (selected.physicalDevice == VK_NULL_HANDLE) {
    throw std::runtime_error("No Vulkan physical device with graphics+present+swapchain support");
  }

  std::vector<const char*> deviceExtensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
  const auto availableExtensions = GetDeviceExtensions(selected.physicalDevice);
  if (HasDeviceExtension(availableExtensions, "VK_KHR_portability_subset")) {
    deviceExtensions.push_back("VK_KHR_portability_subset");
  }

  const float queuePriority = 1.0F;
  VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
  queueInfo.queueFamilyIndex = selected.graphicsQueueFamily;
  queueInfo.queueCount = 1;
  queueInfo.pQueuePriorities = &queuePriority;

  VkPhysicalDeviceFeatures supportedFeatures{};
  vkGetPhysicalDeviceFeatures(selected.physicalDevice, &supportedFeatures);

  VkPhysicalDeviceFeatures features{};
  features.samplerAnisotropy = supportedFeatures.samplerAnisotropy;

  VkDeviceCreateInfo createInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
  createInfo.queueCreateInfoCount = 1;
  createInfo.pQueueCreateInfos = &queueInfo;
  createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
  createInfo.ppEnabledExtensionNames = deviceExtensions.data();
  createInfo.pEnabledFeatures = &features;

  VkDevice device = VK_NULL_HANDLE;
  VkCheck(vkCreateDevice(selected.physicalDevice, &createInfo, nullptr, &device), "vkCreateDevice failed");

  VkQueue queue = VK_NULL_HANDLE;
  vkGetDeviceQueue(device, selected.graphicsQueueFamily, 0, &queue);

  return VulkanDevice(selected.physicalDevice, device, selected.graphicsQueueFamily, queue);
}

}  // namespace vv
