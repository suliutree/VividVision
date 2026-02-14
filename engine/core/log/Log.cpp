#include "core/log/Log.hpp"

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace vv::log {

static std::shared_ptr<spdlog::logger> g_logger;

void Initialize() {
  if (g_logger) {
    return;
  }
  g_logger = spdlog::stdout_color_mt("vividvision");
  g_logger->set_pattern("[%H:%M:%S] [%^%l%$] %v");
  g_logger->set_level(spdlog::level::info);
}

std::shared_ptr<spdlog::logger> Get() {
  if (!g_logger) {
    Initialize();
  }
  return g_logger;
}

}  // namespace vv::log
