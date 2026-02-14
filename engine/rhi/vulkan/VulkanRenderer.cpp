#include "rhi/vulkan/VulkanRenderer.hpp"

#include <array>
#include <stdexcept>

#include "rhi/vulkan/VulkanCheck.hpp"

namespace vv {

VulkanRenderer::~VulkanRenderer() {
  Shutdown();
}

void VulkanRenderer::Initialize(IWindow& window, bool enableValidation) {
  if (initialized_) {
    return;
  }

  window_ = &window;

  InstanceDesc instanceDesc;
  instanceDesc.requiredExtensions = window.GetRequiredVulkanInstanceExtensions();
  instanceDesc.enableValidation = enableValidation;

  instance_ = VulkanInstance::Create(instanceDesc);
  surface_ = VulkanSurface(instance_.Get(), window.CreateVulkanSurface(instance_.Get()));
  device_ = VulkanDevice::Create(instance_.Get(), surface_.Get());
  swapchain_ = VulkanSwapchain::Create(device_.PhysicalDevice(),
                                       device_.Get(),
                                       surface_.Get(),
                                       device_.GraphicsQueueFamily(),
                                       window.GetFramebufferSize());

  CreateRenderPass();
  CreateDepthResources();
  CreateFramebuffers();
  CreateCommandResources();
  CreateSyncObjects();
  skinPbrPass_.Initialize(device_.PhysicalDevice(),
                          device_.Get(),
                          device_.GraphicsQueue(),
                          device_.GraphicsQueueFamily(),
                          renderPass_,
                          swapchain_.Extent(),
                          "build/shaders");

  initialized_ = true;
}

void VulkanRenderer::Shutdown() {
  if (!initialized_) {
    return;
  }

  vkDeviceWaitIdle(device_.Get());

  skinPbrPass_.Shutdown();
  DestroySyncObjects();
  DestroyCommandResources();
  DestroyFramebuffers();
  DestroyDepthResources();

  if (renderPass_ != VK_NULL_HANDLE) {
    vkDestroyRenderPass(device_.Get(), renderPass_, nullptr);
    renderPass_ = VK_NULL_HANDLE;
  }

  swapchain_ = VulkanSwapchain{};
  device_ = VulkanDevice{};
  surface_ = VulkanSurface{};
  instance_ = VulkanInstance{};

  initialized_ = false;
}

void VulkanRenderer::CreateRenderPass() {
  VkAttachmentDescription colorAttachment{};
  colorAttachment.format = swapchain_.ImageFormat();
  colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
  colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

  VkAttachmentDescription depthAttachment{};
  depthAttachment.format = depthFormat_;
  depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
  depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkAttachmentReference colorRef{};
  colorRef.attachment = 0;
  colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkAttachmentReference depthRef{};
  depthRef.attachment = 1;
  depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpass{};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &colorRef;
  subpass.pDepthStencilAttachment = &depthRef;

  VkSubpassDependency dependency{};
  dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
  dependency.dstSubpass = 0;
  dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

  std::array<VkAttachmentDescription, 2> attachments{colorAttachment, depthAttachment};

  VkRenderPassCreateInfo rpInfo{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
  rpInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
  rpInfo.pAttachments = attachments.data();
  rpInfo.subpassCount = 1;
  rpInfo.pSubpasses = &subpass;
  rpInfo.dependencyCount = 1;
  rpInfo.pDependencies = &dependency;

  VkCheck(vkCreateRenderPass(device_.Get(), &rpInfo, nullptr, &renderPass_), "vkCreateRenderPass failed");
}

uint32_t VulkanRenderer::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const {
  VkPhysicalDeviceMemoryProperties memProperties{};
  vkGetPhysicalDeviceMemoryProperties(device_.PhysicalDevice(), &memProperties);

  for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
    if (((typeFilter & (1U << i)) != 0U) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
      return i;
    }
  }
  throw std::runtime_error("Unable to find suitable Vulkan memory type");
}

void VulkanRenderer::CreateDepthResources() {
  const VkExtent2D extent = swapchain_.Extent();

  VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.extent.width = extent.width;
  imageInfo.extent.height = extent.height;
  imageInfo.extent.depth = 1;
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.format = depthFormat_;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  VkCheck(vkCreateImage(device_.Get(), &imageInfo, nullptr, &depthImage_), "vkCreateImage(depth) failed");

  VkMemoryRequirements memRequirements{};
  vkGetImageMemoryRequirements(device_.Get(), depthImage_, &memRequirements);

  VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  VkCheck(vkAllocateMemory(device_.Get(), &allocInfo, nullptr, &depthMemory_), "vkAllocateMemory(depth) failed");
  VkCheck(vkBindImageMemory(device_.Get(), depthImage_, depthMemory_, 0), "vkBindImageMemory(depth) failed");

  VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  viewInfo.image = depthImage_;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = depthFormat_;
  viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;

  VkCheck(vkCreateImageView(device_.Get(), &viewInfo, nullptr, &depthImageView_), "vkCreateImageView(depth) failed");
}

void VulkanRenderer::DestroyDepthResources() {
  if (depthImageView_ != VK_NULL_HANDLE) {
    vkDestroyImageView(device_.Get(), depthImageView_, nullptr);
    depthImageView_ = VK_NULL_HANDLE;
  }
  if (depthImage_ != VK_NULL_HANDLE) {
    vkDestroyImage(device_.Get(), depthImage_, nullptr);
    depthImage_ = VK_NULL_HANDLE;
  }
  if (depthMemory_ != VK_NULL_HANDLE) {
    vkFreeMemory(device_.Get(), depthMemory_, nullptr);
    depthMemory_ = VK_NULL_HANDLE;
  }
}

void VulkanRenderer::CreateFramebuffers() {
  framebuffers_.resize(swapchain_.ImageViews().size());

  for (size_t i = 0; i < swapchain_.ImageViews().size(); ++i) {
    std::array<VkImageView, 2> attachments = {
        swapchain_.ImageViews()[i],
        depthImageView_,
    };

    VkFramebufferCreateInfo fbInfo{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fbInfo.renderPass = renderPass_;
    fbInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    fbInfo.pAttachments = attachments.data();
    fbInfo.width = swapchain_.Extent().width;
    fbInfo.height = swapchain_.Extent().height;
    fbInfo.layers = 1;

    VkCheck(vkCreateFramebuffer(device_.Get(), &fbInfo, nullptr, &framebuffers_[i]), "vkCreateFramebuffer failed");
  }
}

void VulkanRenderer::DestroyFramebuffers() {
  for (VkFramebuffer framebuffer : framebuffers_) {
    vkDestroyFramebuffer(device_.Get(), framebuffer, nullptr);
  }
  framebuffers_.clear();
}

void VulkanRenderer::CreateCommandResources() {
  VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  poolInfo.queueFamilyIndex = device_.GraphicsQueueFamily();
  VkCheck(vkCreateCommandPool(device_.Get(), &poolInfo, nullptr, &commandPool_), "vkCreateCommandPool failed");

  commandBuffers_.resize(framebuffers_.size());
  VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  allocInfo.commandPool = commandPool_;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers_.size());
  VkCheck(vkAllocateCommandBuffers(device_.Get(), &allocInfo, commandBuffers_.data()), "vkAllocateCommandBuffers failed");
}

void VulkanRenderer::DestroyCommandResources() {
  if (commandPool_ != VK_NULL_HANDLE) {
    vkDestroyCommandPool(device_.Get(), commandPool_, nullptr);
    commandPool_ = VK_NULL_HANDLE;
  }
  commandBuffers_.clear();
}

void VulkanRenderer::CreateSyncObjects() {
  VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

  for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
    VkCheck(vkCreateSemaphore(device_.Get(), &semaphoreInfo, nullptr, &imageAvailable_[i]), "vkCreateSemaphore(imageAvailable) failed");
    VkCheck(vkCreateSemaphore(device_.Get(), &semaphoreInfo, nullptr, &renderFinished_[i]), "vkCreateSemaphore(renderFinished) failed");
    VkCheck(vkCreateFence(device_.Get(), &fenceInfo, nullptr, &inFlight_[i]), "vkCreateFence failed");
  }
}

void VulkanRenderer::DestroySyncObjects() {
  for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
    if (imageAvailable_[i] != VK_NULL_HANDLE) {
      vkDestroySemaphore(device_.Get(), imageAvailable_[i], nullptr);
      imageAvailable_[i] = VK_NULL_HANDLE;
    }
    if (renderFinished_[i] != VK_NULL_HANDLE) {
      vkDestroySemaphore(device_.Get(), renderFinished_[i], nullptr);
      renderFinished_[i] = VK_NULL_HANDLE;
    }
    if (inFlight_[i] != VK_NULL_HANDLE) {
      vkDestroyFence(device_.Get(), inFlight_[i], nullptr);
      inFlight_[i] = VK_NULL_HANDLE;
    }
  }
}

void VulkanRenderer::RecreateSwapchain() {
  Extent2D extent = window_->GetFramebufferSize();
  if (extent.width == 0 || extent.height == 0) {
    return;
  }

  vkDeviceWaitIdle(device_.Get());

  DestroyFramebuffers();
  DestroyDepthResources();

  swapchain_.Recreate(device_.PhysicalDevice(), device_.Get(), surface_.Get(), device_.GraphicsQueueFamily(), extent);

  if (renderPass_ != VK_NULL_HANDLE) {
    vkDestroyRenderPass(device_.Get(), renderPass_, nullptr);
    renderPass_ = VK_NULL_HANDLE;
  }

  CreateRenderPass();
  CreateDepthResources();
  CreateFramebuffers();
  skinPbrPass_.RecreateForRenderPass(renderPass_, swapchain_.Extent());

  if (!commandBuffers_.empty()) {
    commandBuffers_.clear();
  }
  if (commandPool_ != VK_NULL_HANDLE) {
    vkDestroyCommandPool(device_.Get(), commandPool_, nullptr);
    commandPool_ = VK_NULL_HANDLE;
  }
  CreateCommandResources();
}

void VulkanRenderer::RecordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex, const RenderScene& scene, const FrameContext& frame) {
  VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  VkCheck(vkBeginCommandBuffer(cmd, &beginInfo), "vkBeginCommandBuffer failed");

  skinPbrPass_.PrepareFrame(currentFrame_, scene, frame);
  skinPbrPass_.RenderShadow(cmd, currentFrame_, scene);

  std::array<VkClearValue, 2> clearValues{};
  clearValues[0].color = {{0.08F, 0.09F, 0.12F, 1.0F}};
  clearValues[1].depthStencil = {1.0F, 0};

  VkRenderPassBeginInfo rpBegin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
  rpBegin.renderPass = renderPass_;
  rpBegin.framebuffer = framebuffers_[imageIndex];
  rpBegin.renderArea.offset = {0, 0};
  rpBegin.renderArea.extent = swapchain_.Extent();
  rpBegin.clearValueCount = static_cast<uint32_t>(clearValues.size());
  rpBegin.pClearValues = clearValues.data();

  vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
  skinPbrPass_.Render(cmd, currentFrame_, scene, frame);
  vkCmdEndRenderPass(cmd);

  VkCheck(vkEndCommandBuffer(cmd), "vkEndCommandBuffer failed");
}

void VulkanRenderer::RenderFrame(const RenderScene& scene, const FrameContext& frame) {
  if (!initialized_) {
    return;
  }

  if (window_->WasResized()) {
    window_->ResetResizedFlag();
    RecreateSwapchain();
  }

  VkCheck(vkWaitForFences(device_.Get(), 1, &inFlight_[currentFrame_], VK_TRUE, UINT64_MAX), "vkWaitForFences failed");

  const AcquireResult acquire = swapchain_.AcquireNextImage(device_.Get(), imageAvailable_[currentFrame_]);
  if (acquire.result == VK_ERROR_OUT_OF_DATE_KHR) {
    RecreateSwapchain();
    return;
  }
  if (acquire.result != VK_SUCCESS && acquire.result != VK_SUBOPTIMAL_KHR) {
    throw std::runtime_error("vkAcquireNextImageKHR failed");
  }

  VkCheck(vkResetFences(device_.Get(), 1, &inFlight_[currentFrame_]), "vkResetFences failed");
  VkCheck(vkResetCommandBuffer(commandBuffers_[acquire.imageIndex], 0), "vkResetCommandBuffer failed");

  RecordCommandBuffer(commandBuffers_[acquire.imageIndex], acquire.imageIndex, scene, frame);

  VkSemaphore waitSemaphores[] = {imageAvailable_[currentFrame_]};
  VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
  VkSemaphore signalSemaphores[] = {renderFinished_[currentFrame_]};

  VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  submitInfo.waitSemaphoreCount = 1;
  submitInfo.pWaitSemaphores = waitSemaphores;
  submitInfo.pWaitDstStageMask = waitStages;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &commandBuffers_[acquire.imageIndex];
  submitInfo.signalSemaphoreCount = 1;
  submitInfo.pSignalSemaphores = signalSemaphores;

  VkCheck(vkQueueSubmit(device_.GraphicsQueue(), 1, &submitInfo, inFlight_[currentFrame_]), "vkQueueSubmit failed");

  const VkResult presentResult = swapchain_.Present(device_.GraphicsQueue(), acquire.imageIndex, renderFinished_[currentFrame_]);
  if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
    RecreateSwapchain();
  } else if (presentResult != VK_SUCCESS) {
    throw std::runtime_error("vkQueuePresentKHR failed");
  }

  currentFrame_ = (currentFrame_ + 1) % kMaxFramesInFlight;
}

}  // namespace vv
