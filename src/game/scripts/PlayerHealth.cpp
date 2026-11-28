#include "PlayerHealth.h"

#include "game/Events.h"

#include "engine/script/ScriptRegistry.h"
#include "engine/script/ScriptRunner.h"

#include <algorithm>

REGISTER_SCRIPT(PlayerHealth);

void PlayerHealth::fields(ComponentInfo& info)
{
    info.field<&PlayerHealth::current>("current", 0.5f).field<&PlayerHealth::max>("max", 0.5f);
}

void PlayerHealth::onStart(ScriptContext& ctx)
{
    ctx.on<NamedEvent, &PlayerHealth::onNamed>();
    announce(ctx);
}

void PlayerHealth::onUpdate(ScriptContext& ctx, float)
{
    const Input& input = ctx.input();
    const bool down = input.isKeyDown(SDL_SCANCODE_H);
    if (down && !wasDown)
        damage(ctx, 10.0f);
    wasDown = down;
}

void PlayerHealth::onNamed(ScriptContext& ctx, const NamedEvent& event)
{
    if (event.name != NamedEvent::hash("Heal"))
        return;
    current = max;
    announce(ctx);
}

void PlayerHealth::damage(ScriptContext& ctx, float amount)
{
    current = std::max(current - amount, 0.0f);
    announce(ctx);
    ctx.emit(PlayerDamaged{amount, current, max});
}

void PlayerHealth::announce(ScriptContext& ctx) const
{
    ctx.emit(NamedEvent::make("PlayerHealth", current, max, ctx.self));
}
