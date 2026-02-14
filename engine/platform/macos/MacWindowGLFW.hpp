#pragma once

#include <string>

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#include "platform/interface/IWindow.hpp"

namespace vv {

class MacWindowGLFW final : public IWindow {
 public:
  MacWindowGLFW(uint32_t width, uint32_t height, const std::string& title);
  ~MacWindowGLFW() override;

  MacWindowGLFW(const MacWindowGLFW&) = delete;
  MacWindowGLFW& operator=(const MacWindowGLFW&) = delete;

  bool PollEvents() override;
  Extent2D GetFramebufferSize() const override;
  std::vector<const char*> GetRequiredVulkanInstanceExtensions() const override;
  VkSurfaceKHR CreateVulkanSurface(VkInstance instance) const override;
  bool IsKeyPressed(int keyCode) const override;
  bool IsMouseButtonPressed(int buttonCode) const override;
  void GetCursorPosition(double& x, double& y) const override;
  float ConsumeScrollDeltaY() override;
  bool WasResized() const override;
  void ResetResizedFlag() override;

 private:
  static void FramebufferResizeCallback(GLFWwindow* window, int width, int height);
  static void ScrollCallback(GLFWwindow* window, double xoffset, double yoffset);

  GLFWwindow* window_ = nullptr;
  bool resized_ = false;
  float scrollDeltaY_ = 0.0F;
};

}  // namespace vv
