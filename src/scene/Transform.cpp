#include "Transform.h"

#include <glm/gtc/matrix_transform.hpp>

void Transform::setPos(const glm::vec3& p)
{
    pos = p;
}

void Transform::setRot(const glm::quat& r)
{
    rot = glm::normalize(r);
}

void Transform::setScale(const glm::vec3& s)
{
    scale = s;
}

void Transform::setEulerAngles(const glm::vec3& radians)
{
    rot = glm::quat(radians);
}

glm::mat4 Transform::getMatrix() const
{
    glm::mat4 m = glm::translate(glm::mat4(1.0f), pos);
    m *= glm::mat4_cast(rot);
    return glm::scale(m, scale);
}

// OpenGL -Z forward, +X right, +Y up
glm::vec3 Transform::forward() const
{
    return rot * glm::vec3(0.0f, 0.0f, -1.0f);
}

glm::vec3 Transform::right() const
{
    return rot * glm::vec3(1.0f, 0.0f, 0.0f);
}

glm::vec3 Transform::up() const
{
    return rot * glm::vec3(0.0f, 1.0f, 0.0f);
}

// Rotates around a world-space axis
void Transform::rotate(float radians, const glm::vec3& axis)
{
    rot = glm::normalize(glm::angleAxis(radians, glm::normalize(axis)) * rot);
}

void Transform::lookAt(const glm::vec3& target, const glm::vec3& worldUp)
{
    glm::vec3 dir = target - pos;
    if (glm::dot(dir, dir) < 1e-12f)
        return;
    dir = glm::normalize(dir);
    if (glm::abs(glm::dot(dir, glm::normalize(worldUp))) > 0.9999f)
        return;  // looking straight along worldUp is undefined
    rot = glm::quatLookAt(dir, worldUp);
}

void Transform::translate(const glm::vec3& delta)
{
    pos += delta;
}