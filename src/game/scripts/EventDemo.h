#pragma once

#include "game/Events.h"

#include <glm/glm.hpp>

struct ComponentInfo;
struct ScriptContext;
struct NamedEvent;

enum class PingMode
{
    Immediate,
    Queued,
    Both,
};

// Phase 1 test: P sends a Ping, left click logs the mouse in game pixels
struct PingSender
{
    PingMode mode = PingMode::Both;
    glm::vec2 offset{0.0f};
    glm::vec4 tint{1.0f, 0.5f, 0.25f, 1.0f};
    int count = 0;
    bool wasDown = false;

    void onUpdate(ScriptContext& ctx, float dt);
    static void fields(ComponentInfo& info);
};

struct PingLogger
{
    int received = 0;

    void onStart(ScriptContext& ctx);
    void onPing(ScriptContext& ctx, const Ping& ping);
    void onNamed(ScriptContext& ctx, const NamedEvent& event);
    static void fields(ComponentInfo& info);
};
