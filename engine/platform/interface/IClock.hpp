#pragma once

#include <cstdint>

namespace vv {

class IClock {
 public:
  virtual ~IClock() = default;
  virtual uint64_t NowMicros() const = 0;
};

}  // namespace vv
