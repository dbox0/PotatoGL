#pragma once

#include <string>

struct OverlayContext;

class DebugPanel
{
private:
    bool m_showDemo = false;
    bool m_vsync = true;
    std::string m_glVersion;
    std::string m_glRenderer;

public:
    void init();
    void draw(const OverlayContext& context);
};
