//
// Created by dbox on 25.11.26.
//

#include "Camera.h"

#include <glm/gtc/matrix_transform.hpp>

glm::mat4 Camera::getProjection() const
{
    float halfH = m_orthoHeight*0.5f;
    float halfW = halfH * m_aspect;
    return m_orthographic? glm::ortho(-halfW,halfW,-halfH,halfH,m_near,m_far) :
            glm::perspective(m_fovRad,m_aspect,m_near,m_far);
}
