#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

class Transform
{
private:
    glm::vec3 pos = glm::vec3(0.0f);
    glm::quat rot = glm::identity<glm::quat>();
    glm::vec3 scale = glm::vec3(1.0f);

public:
    glm::vec3 getPos()      { return pos;   }
    glm::quat getRot()      { return rot;   }
    glm::vec3 getScale()    { return scale; }

    void setPos(const glm::vec3& p);
    void setRot(const glm::quat& r);
    void setScale(const glm::vec3& s);
    void setEulerAngles(const glm::vec3& radians);  // pitch, yaw, roll

    glm::mat4 getMatrix() const;  // T * R * S

    glm::vec3 forward() const;
    glm::vec3 right() const;
    glm::vec3 up() const;

    void translate(const glm::vec3& delta);
    void rotate(float radians, const glm::vec3& axis);
    void lookAt(const glm::vec3& target, const glm::vec3& worldUp = glm::vec3(0.0f, 1.0f, 0.0f));
};
