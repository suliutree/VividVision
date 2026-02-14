#include "rhi/vulkan/VulkanInstance.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>
#include <unordered_set>

#include "rhi/vulkan/MoltenVkBridge.hpp"
#include "rhi/vulkan/VulkanCheck.hpp"

namespace vv {
namespace {

std::vector<VkExtensionProperties> EnumerateInstanceExtensions() {
  uint32_t count = 0;
  VkCheck(vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr), "vkEnumerateInstanceExtensionProperties count failed");
  std::vector<VkExtensionProperties> out(count);
  VkCheck(vkEnumerateInstanceExtensionProperties(nullptr, &count, out.data()), "vkEnumerateInstanceExtensionProperties failed");
  return out;
}

bool HasExtension(const std::vector<VkExtensionProperties>& extensions, const char* name) {
  return std::any_of(extensions.begin(), extensions.end(), [name](const VkExtensionProperties& ext) {
    return std::strcmp(ext.extensionName, name) == 0;
  });
}

std::vector<const char*> MergeExtensions(const InstanceDesc& desc) {
  std::vector<const char*> merged = desc.requiredExtensions;

#if VV_PLATFORM_MACOS
  for (const char* ext : GetMoltenVkInstanceExtensions()) {
    merged.push_back(ext);
  }
#endif
  if (desc.enableValidation) {
    merged.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  }

  std::unordered_set<std::string> dedupe;
  std::vector<const char*> unique;
  for (const char* ext : merged) {
    if (dedupe.insert(ext).second) {
      unique.push_back(ext);
    }
  }
  return unique;
}

}  // namespace

VulkanInstance::~VulkanInstance() {
  if (instance_ != VK_NULL_HANDLE) {
    vkDestroyInstance(instance_, nullptr);
    instance_ = VK_NULL_HANDLE;
  }
}

VulkanInstance::VulkanInstance(VulkanInstance&& other) noexcept {
  std::swap(instance_, other.instance_);
}

VulkanInstance& VulkanInstance::operator=(VulkanInstance&& other) noexcept {
  if (this != &other) {
    if (instance_ != VK_NULL_HANDLE) {
      vkDestroyInstance(instance_, nullptr);
    }
    instance_ = other.instance_;
    other.instance_ = VK_NULL_HANDLE;
  }
  return *this;
}

VulkanInstance VulkanInstance::Create(const InstanceDesc& desc) {
  const auto available = EnumerateInstanceExtensions();
  const auto required = MergeExtensions(desc);

  for (const char* ext : required) {
    if (!HasExtension(available, ext)) {
      throw std::runtime_error(std::string("Missing Vulkan instance extension: ") + ext);
    }
  }

  VkApplicationInfo appInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  appInfo.pApplicationName = "VividVision";
  appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
  appInfo.pEngineName = "VividVision";
  appInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
  appInfo.apiVersion = VK_API_VERSION_1_2;

  VkInstanceCreateInfo createInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  createInfo.pApplicationInfo = &appInfo;
  createInfo.enabledExtensionCount = static_cast<uint32_t>(required.size());
  createInfo.ppEnabledExtensionNames = required.data();
#if VV_PLATFORM_MACOS
  createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif

  VkInstance instance = VK_NULL_HANDLE;
  VkCheck(vkCreateInstance(&createInfo, nullptr, &instance), "vkCreateInstance failed");

  return VulkanInstance(instance);
}

}  // namespace vv
