#include "Movement.h"

#include "engine/script/ScriptRegistry.h"
#include "engine/script/ScriptRunner.h"

REGISTER_SCRIPT(Movement);

void Movement::fields(ComponentInfo& info)
{
    info.field<&Movement::speed>("speed", 0.1f).field<&Movement::relativeToFacing>("relativeToFacing");
}

void Movement::onUpdate(ScriptContext& ctx, float dt)
{
    const Input& input = ctx.input();
    glm::vec3 dir(0.0f);
    if (input.isKeyDown(SDL_SCANCODE_UP))    dir.z -= 1.0f;
    if (input.isKeyDown(SDL_SCANCODE_DOWN))  dir.z += 1.0f;
    if (input.isKeyDown(SDL_SCANCODE_LEFT))  dir.x -= 1.0f;
    if (input.isKeyDown(SDL_SCANCODE_RIGHT)) dir.x += 1.0f;
    if (dir == glm::vec3(0.0f))
        return;

    Transform& transform = ctx.transform();
    if (relativeToFacing) {
        dir = transform.getRot() * dir;
        dir.y = 0.0f;
        if (glm::dot(dir, dir) < 1e-8f)
            return;
    }
    transform.translate(glm::normalize(dir) * speed * dt);
}
