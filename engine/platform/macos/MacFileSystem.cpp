#include "platform/macos/MacFileSystem.hpp"

#include <fstream>
#include <sstream>

namespace vv {

FileReadResult MacFileSystem::ReadTextFile(const std::string& path) {
  std::ifstream file(path);
  if (!file.is_open()) {
    return FileReadResult{.ok = false, .error = "failed to open file: " + path, .bytes = {}};
  }

  std::ostringstream oss;
  oss << file.rdbuf();
  return FileReadResult{.ok = true, .error = {}, .bytes = oss.str()};
}

}  // namespace vv
