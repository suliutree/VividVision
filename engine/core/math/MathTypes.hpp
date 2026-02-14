#pragma once

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/quaternion_float.hpp>
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace vv {

using Vec2 = glm::vec2;
using Vec3 = glm::vec3;
using Vec4 = glm::vec4;
using Quat = glm::quat;
using Mat4 = glm::mat4;

struct Transform {
  Vec3 translation{0.0F};
  Quat rotation{1.0F, 0.0F, 0.0F, 0.0F};
  Vec3 scale{1.0F};

  [[nodiscard]] Mat4 ToMat4() const {
    Mat4 t = glm::translate(Mat4(1.0F), translation);
    Mat4 r = glm::mat4_cast(rotation);
    Mat4 s = glm::scale(Mat4(1.0F), scale);
    return t * r * s;
  }
};

inline Transform Interpolate(const Transform& a, const Transform& b, float t) {
  Transform out;
  out.translation = glm::mix(a.translation, b.translation, t);
  out.rotation = glm::normalize(glm::slerp(a.rotation, b.rotation, t));
  out.scale = glm::mix(a.scale, b.scale, t);
  return out;
}

}  // namespace vv
