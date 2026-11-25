#pragma once
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_video.h>

#include "engine/scene/Camera.h"

class CameraController
{
private:
    float m_yaw = 0.0f;
    float m_pitch = 0.0f;

    float m_moveSpeed = 5.0f;           
    float m_sprintMultiplier = 3.0f;
    float m_mouseSensitivity = 0.002f;  // radians per pixel

    SDL_MouseButtonFlags m_lookButtons = 0;
    bool m_keyboardEnabled = true;
    float m_mouseDx = 0.0f;  // accumulated since last update
    float m_mouseDy = 0.0f;

public:
    void handleEvent(const SDL_Event& event, SDL_Window* window);
    void update(float dt, Camera& camera);

    // Takes yaw/pitch from the camera's current orientation, e.g. after lookAt
    void syncFrom(const Camera& camera);

    void setKeyboardEnabled(bool enabled)   { m_keyboardEnabled = enabled; }
    bool isLooking() const                  { return m_lookButtons != 0; }

    float getMoveSpeed() const              { return m_moveSpeed; }
    void setMoveSpeed(float speed)          { m_moveSpeed = speed; }
    void setMouseSensitivity(float radPerPx) { m_mouseSensitivity = radPerPx; }
};
