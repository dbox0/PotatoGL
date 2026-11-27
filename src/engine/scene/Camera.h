#pragma once
#include "Transform.h"

glm::mat4 makeProjection(bool orthographic, float fovRad, float orthoHeight, float aspect, float zNear, float zFar);
// Inverse of a world matrix, ignoring its scale
glm::mat4 viewFromWorld(const glm::mat4& world);
// Near and far distances of a glm::perspective or glm::ortho matrix
void clipPlanes(const glm::mat4& proj, float& zNear, float& zFar);
inline bool isOrthographic(const glm::mat4& proj) { return proj[3][3] == 1.0f; }

struct ViewMatrices
{
    glm::mat4 view;
    glm::mat4 proj;

    glm::mat4 viewProj() const { return proj * view; }
};


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
    // Rigid inverse: ignores the transform's scale
    glm::mat4 getView() const
    {
        const glm::mat3 invRot = glm::mat3_cast(glm::conjugate(m_transform.getRot()));
        glm::mat4 view(invRot);
        view[3] = glm::vec4(-(invRot * m_transform.getPos()), 1.0f);
        return view;
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
