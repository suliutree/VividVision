#pragma once

#include <GLFW/glfw3.h>

namespace vv {

struct DemoInputMap {
  static constexpr int kPause = GLFW_KEY_SPACE;
  static constexpr int kNextClip = GLFW_KEY_N;
  static constexpr int kPrevClip = GLFW_KEY_P;
  static constexpr int kSpeedUp = GLFW_KEY_EQUAL;
  static constexpr int kSpeedDown = GLFW_KEY_MINUS;
  static constexpr int kToggleNormalMap = GLFW_KEY_1;
  static constexpr int kToggleSpecularIbl = GLFW_KEY_2;
  static constexpr int kOrbitButton = GLFW_MOUSE_BUTTON_RIGHT;
};

}  // namespace vv
