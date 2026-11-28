#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

namespace potato::Math
{

using Vec2 = glm::vec2;
using Vec3 = glm::vec3;
using Vec4 = glm::vec4;
using IVec2 = glm::ivec2;
using IVec3 = glm::ivec3;
using IVec4 = glm::ivec4;
using Quat = glm::quat;
using Mat3 = glm::mat3;
using Mat4 = glm::mat4;

inline constexpr float Pi = glm::pi<float>();
inline constexpr float TwoPi = glm::two_pi<float>();
inline constexpr float HalfPi = glm::half_pi<float>();
inline constexpr float DegToRad = Pi / 180.0f;
inline constexpr float RadToDeg = 180.0f / Pi;

} // namespace potato::Math
