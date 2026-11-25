#pragma once

#include "engine/app/Game.h"

class Gameplay final : public Game
{
private:
    static constexpr float FixedStep = 1.0f / 60.0f;
    float m_accumulator = 0.0f;

    void fixedUpdate(const GameContext& context, float dt);

public:
    bool init(const GameContext& context) override;
    void update(const GameContext& context, float dt) override;
    void shutdown(const GameContext& context) override;
};
