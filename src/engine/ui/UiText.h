#pragma once

#include <glm/glm.hpp>

class Font;
struct UIText;
struct UITextLayout;

// True if out was stale for text in a rect of size and has been rebuilt
bool layoutText(UITextLayout& out, const UIText& text, Font& font, glm::vec2 size);
