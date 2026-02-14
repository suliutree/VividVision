#pragma once

#include "platform/interface/IThreading.hpp"

namespace vv {

class MacThreading final : public IThreading {
 public:
  void RunAsync(std::function<void()> fn) override;
};

}  // namespace vv
