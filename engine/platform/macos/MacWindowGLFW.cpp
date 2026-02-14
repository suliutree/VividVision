#include "platform/macos/MacWindowGLFW.hpp"

#include <sstream>
#include <stdexcept>
#include <vector>

#include "core/log/Log.hpp"

namespace vv {

namespace {

void EnsureGlfwInitialized() {
  static bool initialized = false;
  if (!initialized) {
    // Must be set before glfwInit; otherwise GLFW ignores this loader hint.
    glfwInitVulkanLoader(vkGetInstanceProcAddr);
    if (glfwInit() != GLFW_TRUE) {
      throw std::runtime_error("glfwInit failed");
    }
    initialized = true;
  }
}

}  // namespace

MacWindowGLFW::MacWindowGLFW(uint32_t width, uint32_t height, const std::string& title) {
  EnsureGlfwInitialized();
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
  window_ = glfwCreateWindow(static_cast<int>(width), static_cast<int>(height), title.c_str(), nullptr, nullptr);
  if (window_ == nullptr) {
    throw std::runtime_error("glfwCreateWindow failed");
  }
  glfwSetWindowUserPointer(window_, this);
  glfwSetFramebufferSizeCallback(window_, FramebufferResizeCallback);
  glfwSetScrollCallback(window_, ScrollCallback);
}

MacWindowGLFW::~MacWindowGLFW() {
  if (window_ != nullptr) {
    glfwDestroyWindow(window_);
    window_ = nullptr;
  }
}

bool MacWindowGLFW::PollEvents() {
  glfwPollEvents();
  return glfwWindowShouldClose(window_) == 0;
}

Extent2D MacWindowGLFW::GetFramebufferSize() const {
  int width = 0;
  int height = 0;
  glfwGetFramebufferSize(window_, &width, &height);
  return Extent2D{static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
}

std::vector<const char*> MacWindowGLFW::GetRequiredVulkanInstanceExtensions() const {
  uint32_t extensionCount = 0;
  const char** extensions = glfwGetRequiredInstanceExtensions(&extensionCount);
  if (extensions == nullptr || extensionCount == 0) {
    const char* errDesc = nullptr;
    const int errCode = glfwGetError(&errDesc);
    std::ostringstream oss;
    oss << "glfwGetRequiredInstanceExtensions failed";
    if (errCode != 0 && errDesc != nullptr) {
      oss << " (GLFW error " << errCode << ": " << errDesc << ")";
    }
    throw std::runtime_error(oss.str());
  }
  return std::vector<const char*>(extensions, extensions + extensionCount);
}

VkSurfaceKHR MacWindowGLFW::CreateVulkanSurface(VkInstance instance) const {
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  const VkResult rc = glfwCreateWindowSurface(instance, window_, nullptr, &surface);
  if (rc != VK_SUCCESS) {
    throw std::runtime_error("glfwCreateWindowSurface failed");
  }
  return surface;
}

bool MacWindowGLFW::IsKeyPressed(int keyCode) const {
  return glfwGetKey(window_, keyCode) == GLFW_PRESS;
}

bool MacWindowGLFW::IsMouseButtonPressed(int buttonCode) const {
  return glfwGetMouseButton(window_, buttonCode) == GLFW_PRESS;
}

void MacWindowGLFW::GetCursorPosition(double& x, double& y) const {
  glfwGetCursorPos(window_, &x, &y);
}

float MacWindowGLFW::ConsumeScrollDeltaY() {
  const float out = scrollDeltaY_;
  scrollDeltaY_ = 0.0F;
  return out;
}

bool MacWindowGLFW::WasResized() const {
  return resized_;
}

void MacWindowGLFW::ResetResizedFlag() {
  resized_ = false;
}

void MacWindowGLFW::FramebufferResizeCallback(GLFWwindow* window, int, int) {
  auto* self = reinterpret_cast<MacWindowGLFW*>(glfwGetWindowUserPointer(window));
  if (self != nullptr) {
    self->resized_ = true;
  }
}

void MacWindowGLFW::ScrollCallback(GLFWwindow* window, double, double yoffset) {
  auto* self = reinterpret_cast<MacWindowGLFW*>(glfwGetWindowUserPointer(window));
  if (self != nullptr) {
    self->scrollDeltaY_ += static_cast<float>(yoffset);
  }
}

}  // namespace vv
