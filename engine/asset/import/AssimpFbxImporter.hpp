#pragma once

#include <string>

#include "core/types/CommonTypes.hpp"
#include "render/scene/SceneTypes.hpp"

namespace vv {

struct ImportOptions {
  bool convertToMeters = true;
  bool forceRightHanded = true;
  uint32_t maxBoneInfluence = 4;
};

struct ImportError {
  std::string message;
};

class AssimpFbxImporter {
 public:
  LoadResult<Scene> Import(const std::string& path, const ImportOptions& opt) const;
};

}  // namespace vv
