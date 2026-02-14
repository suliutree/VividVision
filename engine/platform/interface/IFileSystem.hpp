#pragma once

#include <string>

#include "core/types/CommonTypes.hpp"

namespace vv {

class IFileSystem {
 public:
  virtual ~IFileSystem() = default;
  virtual FileReadResult ReadTextFile(const std::string& path) = 0;
};

}  // namespace vv
