#include "platform/macos/MacClock.hpp"

#include <chrono>

namespace vv {

uint64_t MacClock::NowMicros() const {
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(now).count());
}

}  // namespace vv
