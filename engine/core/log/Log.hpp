#pragma once

#include <memory>

#include <spdlog/logger.h>

namespace vv::log {

void Initialize();
std::shared_ptr<spdlog::logger> Get();

}  // namespace vv::log
