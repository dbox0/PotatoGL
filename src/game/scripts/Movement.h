#pragma once

struct ComponentInfo;
struct ScriptContext;

// Arrow keys move the object on the ground plane (XZ of its parent space)
struct Movement
{
    float speed = 5.0f;
    bool relativeToFacing = false;  // up = the object's forward instead of -Z

    void onUpdate(ScriptContext& ctx, float dt);
    static void fields(ComponentInfo& info);
};
