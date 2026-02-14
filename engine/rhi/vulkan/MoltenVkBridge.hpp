#pragma once

#include <vector>

namespace vv {

inline std::vector<const char*> GetMoltenVkInstanceExtensions() {
  return {
    "VK_KHR_portability_enumeration",
    "VK_KHR_get_physical_device_properties2",
  };
}

}  // namespace vv
