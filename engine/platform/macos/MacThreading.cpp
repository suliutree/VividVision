#include "platform/macos/MacThreading.hpp"

#include <thread>

namespace vv {

void MacThreading::RunAsync(std::function<void()> fn) {
  std::thread worker(std::move(fn));
  worker.detach();
}

}  // namespace vv
