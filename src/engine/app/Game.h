#pragma once

class Camera;
class Renderer;
class Scene;

struct GameContext
{
    Renderer& renderer;
    Scene& scene;
    Camera& camera;
};

// Gameplay module plugged into App. The engine never depends on the implementation.
class Game
{
public:
    virtual ~Game() = default;

    virtual bool init(const GameContext& context) = 0;
    virtual void update(const GameContext& context, float dt) = 0;
    virtual void shutdown(const GameContext& context) = 0;
};
