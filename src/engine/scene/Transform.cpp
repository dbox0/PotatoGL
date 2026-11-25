#include "Transform.h"

void Transform::setPos(const glm::vec3& p)
{
    m_pos = p;
}

void Transform::setRot(const glm::quat& r)
{
    m_rot = glm::normalize(r);
}

void Transform::setScale(const glm::vec3& s)
{
    m_scale = s;
}

void Transform::setEulerAngles(const glm::vec3& radians)
{
    m_rot = glm::quat(radians);
}

glm::mat4 Transform::getMatrix() const
{
    const glm::mat3 r = glm::mat3_cast(m_rot);
    return glm::mat4(glm::vec4(r[0] * m_scale.x, 0.0f),
                     glm::vec4(r[1] * m_scale.y, 0.0f),
                     glm::vec4(r[2] * m_scale.z, 0.0f),
                     glm::vec4(m_pos, 1.0f));
}

// OpenGL -Z forward, +X right, +Y up
glm::vec3 Transform::forward() const
{
    return m_rot * glm::vec3(0.0f, 0.0f, -1.0f);
}

glm::vec3 Transform::right() const
{
    return m_rot * glm::vec3(1.0f, 0.0f, 0.0f);
}

glm::vec3 Transform::up() const
{
    return m_rot * glm::vec3(0.0f, 1.0f, 0.0f);
}

// Rotates around a world-space axis
void Transform::rotate(float radians, const glm::vec3& axis)
{
    m_rot = glm::normalize(glm::angleAxis(radians, glm::normalize(axis)) * m_rot);
}

void Transform::lookAt(const glm::vec3& target, const glm::vec3& worldUp)
{
    glm::vec3 dir = target - m_pos;
    if (glm::dot(dir, dir) < 1e-12f)
        return;
    dir = glm::normalize(dir);
    if (glm::abs(glm::dot(dir, glm::normalize(worldUp))) > 0.9999f)
        return;  // looking straight along worldUp is undefined
    m_rot = glm::quatLookAt(dir, worldUp);
}

void Transform::translate(const glm::vec3& delta)
{
    m_pos += delta;
}