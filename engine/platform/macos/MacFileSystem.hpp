#pragma once

#include "platform/interface/IFileSystem.hpp"

namespace vv {

class MacFileSystem final : public IFileSystem {
 public:
  FileReadResult ReadTextFile(const std::string& path) override;
};

}  // namespace vv
