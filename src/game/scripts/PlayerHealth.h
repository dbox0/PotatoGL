#pragma once

struct ComponentInfo;
struct NamedEvent;
struct ScriptContext;

// UI sample: announces its health as NamedEvent "PlayerHealth" (bars listen for it) and as
// PlayerDamaged. H deals 10 damage; the named event "Heal" (a button's onClick) restores it.
struct PlayerHealth
{
    float current = 100.0f;
    float max = 100.0f;
    bool wasDown = false;

    void onStart(ScriptContext& ctx);
    void onUpdate(ScriptContext& ctx, float dt);
    void onNamed(ScriptContext& ctx, const NamedEvent& event);
    void damage(ScriptContext& ctx, float amount);
    static void fields(ComponentInfo& info);

private:
    void announce(ScriptContext& ctx) const;
};
