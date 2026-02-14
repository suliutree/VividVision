#include "rhi/vulkan/VulkanSwapchain.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <utility>

#include "rhi/vulkan/VulkanCheck.hpp"

namespace vv {
namespace {

VkSurfaceFormatKHR ChooseFormat(const std::vector<VkSurfaceFormatKHR>& formats) {
  constexpr std::array<VkFormat, 5> preferredFormats = {
      VK_FORMAT_A2B10G10R10_UNORM_PACK32,
      VK_FORMAT_A2R10G10B10_UNORM_PACK32,
      VK_FORMAT_B8G8R8A8_UNORM,
      VK_FORMAT_R8G8B8A8_UNORM,
  };

  for (const VkFormat preferred : preferredFormats) {
    for (const auto& format : formats) {
      if (format.format == preferred && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
        return format;
      }
    }
  }

  for (const auto& format : formats) {
    if (format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR &&
        format.format != VK_FORMAT_B8G8R8A8_SRGB &&
        format.format != VK_FORMAT_R8G8B8A8_SRGB) {
      return format;
    }
  }

  return formats.front();
}

VkPresentModeKHR ChoosePresentMode(const std::vector<VkPresentModeKHR>& modes) {
  for (const auto mode : modes) {
    if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
      return mode;
    }
  }
  return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D ChooseExtent(const VkSurfaceCapabilitiesKHR& caps, Extent2D requested) {
  if (caps.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
    return caps.currentExtent;
  }

  VkExtent2D extent;
  extent.width = std::clamp(requested.width, caps.minImageExtent.width, caps.maxImageExtent.width);
  extent.height = std::clamp(requested.height, caps.minImageExtent.height, caps.maxImageExtent.height);
  return extent;
}

}  // namespace

VulkanSwapchain::~VulkanSwapchain() {
  if (device_ != VK_NULL_HANDLE) {
    Cleanup(device_);
  }
}

VulkanSwapchain::VulkanSwapchain(VulkanSwapchain&& other) noexcept {
  std::swap(swapchain_, other.swapchain_);
  std::swap(device_, other.device_);
  std::swap(imageFormat_, other.imageFormat_);
  std::swap(extent_, other.extent_);
  std::swap(images_, other.images_);
  std::swap(imageViews_, other.imageViews_);
}

VulkanSwapchain& VulkanSwapchain::operator=(VulkanSwapchain&& other) noexcept {
  if (this != &other) {
    if (device_ != VK_NULL_HANDLE) {
      Cleanup(device_);
    }
    swapchain_ = other.swapchain_;
    device_ = other.device_;
    imageFormat_ = other.imageFormat_;
    extent_ = other.extent_;
    images_ = std::move(other.images_);
    imageViews_ = std::move(other.imageViews_);

    other.swapchain_ = VK_NULL_HANDLE;
    other.device_ = VK_NULL_HANDLE;
    other.extent_ = {};
  }
  return *this;
}

void VulkanSwapchain::Cleanup(VkDevice device) {
  for (VkImageView view : imageViews_) {
    vkDestroyImageView(device, view, nullptr);
  }
  imageViews_.clear();
  images_.clear();

  if (swapchain_ != VK_NULL_HANDLE) {
    vkDestroySwapchainKHR(device, swapchain_, nullptr);
    swapchain_ = VK_NULL_HANDLE;
  }
}

VulkanSwapchain VulkanSwapchain::Create(VkPhysicalDevice physicalDevice,
                                        VkDevice device,
                                        VkSurfaceKHR surface,
                                        uint32_t queueFamily,
                                        Extent2D windowExtent) {
  VulkanSwapchain out;
  out.Recreate(physicalDevice, device, surface, queueFamily, windowExtent);
  return out;
}

void VulkanSwapchain::Recreate(VkPhysicalDevice physicalDevice,
                               VkDevice device,
                               VkSurfaceKHR surface,
                               uint32_t queueFamily,
                               Extent2D windowExtent) {
  Cleanup(device);
  device_ = device;

  VkSurfaceCapabilitiesKHR caps{};
  VkCheck(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &caps),
          "vkGetPhysicalDeviceSurfaceCapabilitiesKHR failed");

  uint32_t formatCount = 0;
  VkCheck(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr),
          "vkGetPhysicalDeviceSurfaceFormatsKHR count failed");
  std::vector<VkSurfaceFormatKHR> formats(formatCount);
  VkCheck(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, formats.data()),
          "vkGetPhysicalDeviceSurfaceFormatsKHR failed");

  uint32_t presentModeCount = 0;
  VkCheck(vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, nullptr),
          "vkGetPhysicalDeviceSurfacePresentModesKHR count failed");
  std::vector<VkPresentModeKHR> presentModes(presentModeCount);
  VkCheck(vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, presentModes.data()),
          "vkGetPhysicalDeviceSurfacePresentModesKHR failed");

  const VkSurfaceFormatKHR format = ChooseFormat(formats);
  const VkPresentModeKHR presentMode = ChoosePresentMode(presentModes);
  const VkExtent2D swapExtent = ChooseExtent(caps, windowExtent);

  uint32_t imageCount = caps.minImageCount + 1;
  if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) {
    imageCount = caps.maxImageCount;
  }

  VkSwapchainCreateInfoKHR createInfo{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
  createInfo.surface = surface;
  createInfo.minImageCount = imageCount;
  createInfo.imageFormat = format.format;
  createInfo.imageColorSpace = format.colorSpace;
  createInfo.imageExtent = swapExtent;
  createInfo.imageArrayLayers = 1;
  createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  createInfo.preTransform = caps.currentTransform;
  createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  createInfo.presentMode = presentMode;
  createInfo.clipped = VK_TRUE;
  createInfo.oldSwapchain = VK_NULL_HANDLE;
  createInfo.queueFamilyIndexCount = 1;
  createInfo.pQueueFamilyIndices = &queueFamily;

  VkCheck(vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapchain_), "vkCreateSwapchainKHR failed");

  uint32_t outImageCount = 0;
  VkCheck(vkGetSwapchainImagesKHR(device, swapchain_, &outImageCount, nullptr), "vkGetSwapchainImagesKHR count failed");
  images_.resize(outImageCount);
  VkCheck(vkGetSwapchainImagesKHR(device, swapchain_, &outImageCount, images_.data()), "vkGetSwapchainImagesKHR failed");

  imageFormat_ = format.format;
  extent_ = swapExtent;
  imageViews_.resize(images_.size());

  for (size_t i = 0; i < images_.size(); ++i) {
    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = images_[i];
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = imageFormat_;
    viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    VkCheck(vkCreateImageView(device, &viewInfo, nullptr, &imageViews_[i]), "vkCreateImageView failed");
  }
}

AcquireResult VulkanSwapchain::AcquireNextImage(VkDevice device, VkSemaphore imageAvailable) {
  AcquireResult out;
  out.result = vkAcquireNextImageKHR(device, swapchain_, UINT64_MAX, imageAvailable, VK_NULL_HANDLE, &out.imageIndex);
  return out;
}

VkResult VulkanSwapchain::Present(VkQueue queue, uint32_t imageIndex, VkSemaphore renderFinished) const {
  VkPresentInfoKHR presentInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
  presentInfo.waitSemaphoreCount = 1;
  presentInfo.pWaitSemaphores = &renderFinished;
  presentInfo.swapchainCount = 1;
  presentInfo.pSwapchains = &swapchain_;
  presentInfo.pImageIndices = &imageIndex;
  return vkQueuePresentKHR(queue, &presentInfo);
}

}  // namespace vv
