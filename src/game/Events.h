#pragma once

// Typed gameplay events, sent through EventBus
struct Ping
{
    int count = 0;
    bool queued = false;
};

struct PlayerDamaged
{
    float amount = 0.0f;
    float current = 0.0f;
    float max = 0.0f;
};
