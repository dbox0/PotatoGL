#include "Rotator.h"

#include "engine/script/ScriptRegistry.h"
#include "engine/script/ScriptRunner.h"

REGISTER_SCRIPT(Rotator);

void Rotator::fields(ComponentInfo& info)
{
    info.field<&Rotator::axis>("Axis", 0.01f).field<&Rotator::degreesPerSec>("Degrees/sec", 1.0f);
}

void Rotator::onUpdate(ScriptContext& ctx, float dt)
{
    if (glm::dot(axis, axis) < 1e-8f)
        return;
    ctx.transform().rotate(glm::radians(degreesPerSec) * dt, axis);
}
