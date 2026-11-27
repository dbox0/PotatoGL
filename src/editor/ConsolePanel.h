#pragma once

#include "engine/core/LogCapture.h"

#include <vector>

// Captured stdout/stderr; opened from Window > Console
class ConsolePanel
{
private:
    std::vector<logcapture::Line> m_lines;
    char m_filter[128] = {};
    bool m_open = true;
    bool m_showOutput = true;
    bool m_showErrors = true;
    bool m_autoScroll = true;

    bool passes(const logcapture::Line& line) const;

public:
    static constexpr const char* WindowName = "Console";

    bool& open() { return m_open; }
    // fallbackDock: where to dock when the saved layout predates the console
    void draw(unsigned int fallbackDock);
};
