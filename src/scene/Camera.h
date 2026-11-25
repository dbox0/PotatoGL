#pragma once
#include "Transform.h"


class Camera
{
private:
    Transform m_transform;
    glm::mat4 m_view;


    float m_fovDeg = 60.0f;
    float m_fovRad = glm::radians(60.0f);
    float m_aspect = 1.0f;
    float m_near = 0.1f;
    float m_far = 100.0f;

    bool m_orthographic = false;
    float m_orthoHeight;
    float m_orthoWidth;


public:
    glm::mat4 getView() const
    {
        return glm::inverse(m_transform.getMatrix());
    }

    glm::mat4 getProjection() const;

    void setFov(float fov_deg);
    void setFovRad(float fov_rad);
    void setAspect(float aspect);
    void setNear(float zNear);
    void setFar(float zFar);
    void setOrthographic(bool orthographic);
    
    Transform& getTransform(){return m_transform;}
    const Transform& getTransform() const {return m_transform;}
};