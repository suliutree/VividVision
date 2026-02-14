#pragma once

#include <array>
#include <vector>

#include <vulkan/vulkan.h>

#include "platform/interface/IWindow.hpp"
#include "render/passes/SkinPbrPass.hpp"
#include "render/scene/RenderScene.hpp"
#include "rhi/vulkan/VulkanDevice.hpp"
#include "rhi/vulkan/VulkanInstance.hpp"
#include "rhi/vulkan/VulkanSurface.hpp"
#include "rhi/vulkan/VulkanSwapchain.hpp"

namespace vv {

class VulkanRenderer {
 public:
  VulkanRenderer() = default;
  ~VulkanRenderer();

  void Initialize(IWindow& window, bool enableValidation = false);
  void RenderFrame(const RenderScene& scene, const FrameContext& frame);
  void Shutdown();

 private:
  void CreateRenderPass();
  void CreateDepthResources();
  void DestroyDepthResources();
  void CreateFramebuffers();
  void DestroyFramebuffers();
  void CreateCommandResources();
  void DestroyCommandResources();
  void CreateSyncObjects();
  void DestroySyncObjects();
  void RecreateSwapchain();
  void RecordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex, const RenderScene& scene, const FrameContext& frame);
  uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;

  static constexpr uint32_t kMaxFramesInFlight = 2;

  IWindow* window_ = nullptr;
  VulkanInstance instance_;
  VulkanSurface surface_;
  VulkanDevice device_;
  VulkanSwapchain swapchain_;

  VkRenderPass renderPass_ = VK_NULL_HANDLE;
  VkFormat depthFormat_ = VK_FORMAT_D32_SFLOAT;
  VkImage depthImage_ = VK_NULL_HANDLE;
  VkDeviceMemory depthMemory_ = VK_NULL_HANDLE;
  VkImageView depthImageView_ = VK_NULL_HANDLE;

  std::vector<VkFramebuffer> framebuffers_;
  VkCommandPool commandPool_ = VK_NULL_HANDLE;
  std::vector<VkCommandBuffer> commandBuffers_;

  std::array<VkSemaphore, kMaxFramesInFlight> imageAvailable_{};
  std::array<VkSemaphore, kMaxFramesInFlight> renderFinished_{};
  std::array<VkFence, kMaxFramesInFlight> inFlight_{};

  uint32_t currentFrame_ = 0;
  bool initialized_ = false;
  SkinPbrPass skinPbrPass_{};
};

}  // namespace vv
