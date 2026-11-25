#include "Rotator.h"

#include "engine/script/ScriptContext.h"

void Rotator::onUpdate(ScriptContext& ctx, float dt)
{
    if (glm::dot(axis, axis) < 1e-8f)
        return;
    ctx.transform().rotate(glm::radians(degreesPerSec) * dt, axis);
}
