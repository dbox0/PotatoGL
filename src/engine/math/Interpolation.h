#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace Math
{

inline constexpr float QuatLinearThreshold = 0.9995f;

// Works for float, double and any glm vector or matrix
template <class T>
constexpr T lerp(const T& a, const T& b, float t)
{
    return a + (b - a) * t;
}

constexpr float inverseLerp(float a, float b, float v)
{
    return a != b ? (v - a) / (b - a) : 0.0f;
}

constexpr float remap(float v, float inMin, float inMax, float outMin, float outMax)
{
    return lerp(outMin, outMax, inverseLerp(inMin, inMax, v));
}

constexpr float smoothstep(float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

constexpr float smootherstep(float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

// Radians, takes the short way around
inline float lerpAngle(float a, float b, float t)
{
    return a + std::remainder(b - a, glm::two_pi<float>()) * t;
}

// Shortest path, renormalized
inline glm::quat nlerp(const glm::quat& a, glm::quat b, float t)
{
    if (glm::dot(a, b) < 0.0f)
        b = -b;
    return glm::normalize(a * (1.0f - t) + b * t);
}

// Shortest path, constant angular velocity
inline glm::quat slerp(const glm::quat& a, glm::quat b, float t)
{
    float d = glm::dot(a, b);
    if (d < 0.0f)
    {
        b = -b;
        d = -d;
    }
    if (d > QuatLinearThreshold)
        return glm::normalize(a * (1.0f - t) + b * t);
    const float theta = std::acos(d);
    const float invSin = 1.0f / std::sin(theta);
    return a * (std::sin((1.0f - t) * theta) * invSin) + b * (std::sin(t * theta) * invSin);
}

// Unit vectors, rotates a towards b along the great circle
inline glm::vec3 slerp(const glm::vec3& a, const glm::vec3& b, float t)
{
    const float d = std::clamp(glm::dot(a, b), -1.0f, 1.0f);
    if (d > QuatLinearThreshold)
        return glm::normalize(lerp(a, b, t));
    glm::vec3 ortho = b - a * d;
    const float len2 = glm::dot(ortho, ortho);
    if (len2 < 1e-12f)
    {
        // Antiparallel: any perpendicular axis will do
        ortho = glm::cross(a, std::abs(a.x) < 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0));
        ortho = glm::normalize(ortho);
    }
    else
        ortho /= std::sqrt(len2);
    const float theta = std::acos(d) * t;
    return a * std::cos(theta) + ortho * std::sin(theta);
}

// Angle in radians of the rotation taking a to b
inline float angleBetween(const glm::quat& a, const glm::quat& b)
{
    const float d = std::min(std::abs(glm::dot(a, b)), 1.0f);
    return 2.0f * std::acos(d);
}

// Frame-rate independent exponential approach; lambda is 1 / time constant
template <class T>
T damp(const T& current, const T& target, float lambda, float dt)
{
    return lerp(current, target, 1.0f - std::exp(-lambda * dt));
}

inline glm::quat damp(const glm::quat& current, const glm::quat& target, float lambda, float dt)
{
    return slerp(current, target, 1.0f - std::exp(-lambda * dt));
}

inline float moveTowards(float current, float target, float maxDelta)
{
    const float d = target - current;
    return std::abs(d) <= maxDelta ? target : current + std::copysign(maxDelta, d);
}

template <glm::length_t L>
glm::vec<L, float> moveTowards(const glm::vec<L, float>& current, const glm::vec<L, float>& target, float maxDelta)
{
    const glm::vec<L, float> d = target - current;
    const float len = glm::length(d);
    if (len <= maxDelta || len < 1e-6f)
        return target;
    return current + d * (maxDelta / len);
}

// maxAngle in radians
inline glm::quat rotateTowards(const glm::quat& current, const glm::quat& target, float maxAngle)
{
    const float angle = angleBetween(current, target);
    if (angle <= maxAngle || angle < 1e-6f)
        return target;
    return slerp(current, target, maxAngle / angle);
}

// Cubic Hermite with tangents m0, m1 (glTF CUBICSPLINE: m = tangent * keyDelta)
template <class T>
constexpr T hermite(const T& p0, const T& m0, const T& p1, const T& m1, float t)
{
    const float t2 = t * t;
    const float t3 = t2 * t;
    return p0 * (2.0f * t3 - 3.0f * t2 + 1.0f) + m0 * (t3 - 2.0f * t2 + t) + p1 * (-2.0f * t3 + 3.0f * t2) +
           m1 * (t3 - t2);
}

// Uniform Catmull-Rom, interpolates between p1 and p2
template <class T>
constexpr T catmullRom(const T& p0, const T& p1, const T& p2, const T& p3, float t)
{
    return hermite(p1, (p2 - p0) * 0.5f, p2, (p3 - p1) * 0.5f, t);
}

template <class T>
constexpr T quadraticBezier(const T& p0, const T& p1, const T& p2, float t)
{
    const float u = 1.0f - t;
    return p0 * (u * u) + p1 * (2.0f * u * t) + p2 * (t * t);
}

template <class T>
constexpr T cubicBezier(const T& p0, const T& p1, const T& p2, const T& p3, float t)
{
    const float u = 1.0f - t;
    return p0 * (u * u * u) + p1 * (3.0f * u * u * t) + p2 * (3.0f * u * t * t) + p3 * (t * t * t);
}

} 
