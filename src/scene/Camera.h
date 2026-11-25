#pragma once
#include "Transform.h"


class Camera
{
private:
    Transform m_transform;

    float m_fovRad = glm::radians(60.0f);
    float m_aspect = 1.0f;
    float m_near = 0.1f;
    float m_far = 100.0f;

    bool m_orthographic = false;
    float m_orthoHeight = 10.0f;  // width = height * aspect


public:
    glm::mat4 getView() const
    {
        return glm::inverse(m_transform.getMatrix());
    }

    glm::mat4 getProjection() const;

    void setFov(float fovDeg)                { m_fovRad         = glm::radians(fovDeg); }
    void setFovRad(float fovRad)             { m_fovRad         = fovRad; }
    void setAspect(float aspect)             { m_aspect         = aspect; }
    void setNear(float zNear)                { m_near           = zNear; }
    void setFar(float zFar)                  { m_far            = zFar; }
    void setOrthographic(bool orthographic)  { m_orthographic   = orthographic; }
    void setOrthoHeight(float height)        { m_orthoHeight    = height; }

    float getFovRad() const               { return m_fovRad; }
    float getFovDeg() const               { return glm::degrees(m_fovRad); }
    float getAspect() const               { return m_aspect; }
    float getNear() const                 { return m_near; }
    float getFar() const                  { return m_far; }
    bool  isOrthographic() const          { return m_orthographic; }
    float getOrthoHeight() const          { return m_orthoHeight; }

    Transform& getTransform()             {return m_transform;}
    const Transform& getTransform() const {return m_transform;}
};
