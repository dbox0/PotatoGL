#pragma once

// Typed gameplay events, sent through EventBus
struct Ping
{
    int count = 0;
    bool queued = false;
};
