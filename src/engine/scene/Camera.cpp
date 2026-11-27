//
// Created by dbox on 25.11.26.
//

#include "Camera.h"

#include <glm/gtc/matrix_transform.hpp>

glm::mat4 makeProjection(bool orthographic, float fovRad, float orthoHeight, float aspect, float zNear, float zFar)
{
    const float halfH = orthoHeight * 0.5f;
    const float halfW = halfH * aspect;
    return orthographic ? glm::ortho(-halfW, halfW, -halfH, halfH, zNear, zFar)
                        : glm::perspective(fovRad, aspect, zNear, zFar);
}

void clipPlanes(const glm::mat4& proj, float& zNear, float& zFar)
{
    const float a = proj[2][2];
    const float b = proj[3][2];
    if (isOrthographic(proj)) {
        zNear = (b + 1.0f) / a;
        zFar = (b - 1.0f) / a;
    } else {
        zNear = b / (a - 1.0f);
        zFar = b / (a + 1.0f);
    }
}

glm::mat4 viewFromWorld(const glm::mat4& world)
{
    const glm::mat3 rot(glm::normalize(glm::vec3(world[0])), glm::normalize(glm::vec3(world[1])),
                        glm::normalize(glm::vec3(world[2])));
    const glm::mat3 inv = glm::transpose(rot);
    glm::mat4 view(inv);
    view[3] = glm::vec4(-(inv * glm::vec3(world[3])), 1.0f);
    return view;
}

glm::mat4 Camera::getProjection() const
{
    return makeProjection(m_orthographic, m_fovRad, m_orthoHeight, m_aspect, m_near, m_far);
}
