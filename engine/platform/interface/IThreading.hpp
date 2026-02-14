#pragma once

#include <functional>

namespace vv {

class IThreading {
 public:
  virtual ~IThreading() = default;
  virtual void RunAsync(std::function<void()> fn) = 0;
};

}  // namespace vv
