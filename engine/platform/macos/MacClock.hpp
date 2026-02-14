#pragma once

#include "platform/interface/IClock.hpp"

namespace vv {

class MacClock final : public IClock {
 public:
  uint64_t NowMicros() const override;
};

}  // namespace vv
