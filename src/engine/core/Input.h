#pragma once

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_mouse.h>

#include <glm/glm.hpp>

// Keyboard and mouse state for gameplay, refreshed once per frame by App.
// Reports no keys while the editor has keyboard focus. Mouse buttons are reported while the
// pointer is over the game view, and a button pressed there keeps reporting until released.
class Input
{
private:
    const bool* m_keys = nullptr;
    int m_keyCount = 0;
    bool m_enabled = true;

    glm::vec2 m_mouse{0.0f};
    SDL_MouseButtonFlags m_down = 0;
    SDL_MouseButtonFlags m_pressed = 0;
    SDL_MouseButtonFlags m_released = 0;
    SDL_MouseButtonFlags m_captured = 0;  // pressed over the game view, not yet released
    SDL_MouseButtonFlags m_pressEdges = 0;
    SDL_MouseButtonFlags m_releaseEdges = 0;
    float m_wheel = 0.0f;
    float m_wheelEdges = 0.0f;

public:
    bool pointerOverUi = false;  // written by uiInputSystem; gameplay ignores clicks while set

    // App passes every SDL event, between frames
    void handleEvent(const SDL_Event& event)
    {
        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
            m_pressEdges |= SDL_BUTTON_MASK(event.button.button);
        else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP)
            m_releaseEdges |= SDL_BUTTON_MASK(event.button.button);
        else if (event.type == SDL_EVENT_MOUSE_WHEEL)
            m_wheelEdges += event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -event.wheel.y : event.wheel.y;
    }

    // mouse: the pointer is over the game view; mousePos is in game pixels, top-left origin
    void update(bool keyboard, bool mouse, glm::vec2 mousePos)
    {
        m_keys = SDL_GetKeyboardState(&m_keyCount);
        m_enabled = keyboard;

        const SDL_MouseButtonFlags held = SDL_GetMouseState(nullptr, nullptr);
        if (mouse || m_captured)
            m_mouse = mousePos;
        m_pressed = mouse ? m_pressEdges : 0;
        m_captured |= m_pressed;
        m_released = m_releaseEdges & m_captured;
        m_down = held & m_captured;
        m_captured &= held;
        m_wheel = mouse ? m_wheelEdges : 0.0f;
        m_pressEdges = 0;
        m_releaseEdges = 0;
        m_wheelEdges = 0.0f;
    }

    bool isKeyDown(SDL_Scancode key) const
    {
        return m_enabled && m_keys && key < m_keyCount && m_keys[key];
    }

    // button: SDL_BUTTON_LEFT, SDL_BUTTON_RIGHT, ...
    glm::vec2 mousePosition() const        { return m_mouse; }
    bool isMouseDown(int button) const    { return (m_down & SDL_BUTTON_MASK(button)) != 0; }
    bool mousePressed(int button) const   { return (m_pressed & SDL_BUTTON_MASK(button)) != 0; }
    bool mouseReleased(int button) const  { return (m_released & SDL_BUTTON_MASK(button)) != 0; }
    float mouseWheel() const              { return m_wheel; }
};
