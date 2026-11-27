//
// Created by dbox on 25.11.26.
//

#include "CameraController.h"

#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_mouse.h>

void CameraController::handleEvent(const SDL_Event& event, SDL_Window* window)
{
    switch (event.type) {
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        if (!(SDL_BUTTON_MASK(event.button.button) & m_lookButtonMask))
            break;
        const bool wasLooking = m_lookButtons != 0;
        if (event.button.down)
            m_lookButtons |= SDL_BUTTON_MASK(event.button.button);
        else
            m_lookButtons &= ~SDL_BUTTON_MASK(event.button.button);
        if (wasLooking != (m_lookButtons != 0))
            SDL_SetWindowRelativeMouseMode(window, m_lookButtons != 0);
        break;
    }
    case SDL_EVENT_MOUSE_MOTION:
        if (m_lookButtons != 0) {
            m_mouseDx += event.motion.xrel;
            m_mouseDy += event.motion.yrel;
        }
        break;
    default:
        break;
    }
}

void CameraController::update(float dt, Camera& camera)
{
    constexpr float maxPitch = glm::radians(89.0f);

    m_yaw -= m_mouseDx * m_mouseSensitivity;
    m_pitch = glm::clamp(m_pitch - m_mouseDy * m_mouseSensitivity, -maxPitch, maxPitch);
    m_mouseDx = 0.0f;
    m_mouseDy = 0.0f;

    Transform& transform = camera.getTransform();
    transform.setRot(glm::angleAxis(m_yaw, glm::vec3(0.0f, 1.0f, 0.0f))
                     * glm::angleAxis(m_pitch, glm::vec3(1.0f, 0.0f, 0.0f)));

    if (!m_keyboardEnabled)
        return;

    const bool* keys = SDL_GetKeyboardState(nullptr);
    glm::vec3 move(0.0f);
    if (keys[SDL_SCANCODE_W]) move += transform.forward();
    if (keys[SDL_SCANCODE_S]) move -= transform.forward();
    if (keys[SDL_SCANCODE_D]) move += transform.right();
    if (keys[SDL_SCANCODE_A]) move -= transform.right();
    if (keys[SDL_SCANCODE_Q]) move.y += 1.0f;
    if (keys[SDL_SCANCODE_E]) move.y -= 1.0f;

    if (glm::dot(move, move) > 0.0f) {
        float speed = m_moveSpeed * (keys[SDL_SCANCODE_LSHIFT] ? m_sprintMultiplier : 1.0f);
        transform.translate(glm::normalize(move) * speed * dt);
    }
}

void CameraController::syncFrom(const Camera& camera)
{
    const glm::vec3 f = camera.getTransform().forward();
    m_pitch = glm::asin(glm::clamp(f.y, -1.0f, 1.0f));
    m_yaw = glm::atan(-f.x, -f.z);
}
