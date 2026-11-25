#pragma once

#include <SDL3/SDL_keyboard.h>

// Keyboard state for gameplay, refreshed once per frame by App.
// Reports nothing while the editor has keyboard focus.
class Input
{
private:
    const bool* m_keys = nullptr;
    int m_keyCount = 0;
    bool m_enabled = true;

public:
    void update(bool enabled)
    {
        m_keys = SDL_GetKeyboardState(&m_keyCount);
        m_enabled = enabled;
    }

    bool isKeyDown(SDL_Scancode key) const
    {
        return m_enabled && m_keys && key < m_keyCount && m_keys[key];
    }
};
