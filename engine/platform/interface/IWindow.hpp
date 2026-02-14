#pragma once

#include <vector>

#include <vulkan/vulkan.h>

#include "core/types/CommonTypes.hpp"

namespace vv {

class IWindow {
 public:
  virtual ~IWindow() = default;

  virtual bool PollEvents() = 0;
  virtual Extent2D GetFramebufferSize() const = 0;
  virtual std::vector<const char*> GetRequiredVulkanInstanceExtensions() const = 0;
  virtual VkSurfaceKHR CreateVulkanSurface(VkInstance instance) const = 0;
  virtual bool IsKeyPressed(int keyCode) const = 0;
  virtual bool IsMouseButtonPressed(int buttonCode) const = 0;
  virtual void GetCursorPosition(double& x, double& y) const = 0;
  virtual float ConsumeScrollDeltaY() = 0;
  virtual bool WasResized() const = 0;
  virtual void ResetResizedFlag() = 0;
};

}  // namespace vv
