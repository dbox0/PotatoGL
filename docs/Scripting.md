# Scripting

Gameplay code comes in two forms. Both live in `src/game` and use the same registry.

| | Systems | Scripts |
| --- | --- | --- |
| Use for | Behaviour shared by many entities | One-off logic on specific objects |
| Written as | Component struct + free function | Struct with hook methods |
| Cost | One loop over the components it needs | One loop per script type and hook |
| Feels like | ECS / Unity DOTS | Unity `MonoBehaviour` |

Start with a script when prototyping. When a behaviour ends up on hundreds of entities
or needs several components at once, turn it into a system.

## Writing a script

A script is a plain struct with data members and any of the hooks below. No base
class, no virtual functions.

```cpp
// src/game/scripts/Bobber.h
#pragma once

struct ScriptContext;

struct Bobber
{
    float height = 0.5f;
    float speed = 2.0f;
    float time = 0.0f;
    float baseY = 0.0f;

    void onStart(ScriptContext& ctx);
    void onUpdate(ScriptContext& ctx, float dt);
};
```

```cpp
// src/game/scripts/Bobber.cpp
#include "Bobber.h"

#include "engine/script/ScriptContext.h"

#include <cmath>

void Bobber::onStart(ScriptContext& ctx)
{
    baseY = ctx.transform().getPos().y;
}

void Bobber::onUpdate(ScriptContext& ctx, float dt)
{
    time += dt;
    glm::vec3 pos = ctx.transform().getPos();
    pos.y = baseY + height * std::sin(time * speed);
    ctx.transform().setPos(pos);
}
```

Then register it in `src/game/scripts/Register.cpp` and add both files to the `game`
library in `CMakeLists.txt`:

```cpp
scripts.add<Bobber>("Bobber")
    .field<&Bobber::height>("Height", 0.01f)
    .field<&Bobber::speed>("Speed", 0.05f);
```

Fields listed with `.field<>()` show up in the inspector; the second argument is the
drag speed. Unlisted members (`time`, `baseY` above) are internal state.

Registration is an explicit list on purpose: self-registering static objects are
silently dropped by the linker in static libraries.

## Hooks

All hooks are optional. A type without a hook gets no loop for it.

| Hook | When |
| --- | --- |
| `void onCreate(ScriptContext&)` | Immediately when the script is added (like `Awake`) |
| `void onStart(ScriptContext&)` | Once, before the script's first update (like `Start`) |
| `void onUpdate(ScriptContext&, float dt)` | Every frame, `dt` = frame time |
| `void onFixedUpdate(ScriptContext&, float dt)` | Every fixed step, 60 Hz, `dt` = 1/60 |
| `void onLateUpdate(ScriptContext&, float dt)` | Every frame, after all updates (camera follow etc.) |
| `void onDestroy(ScriptContext&)` | When the script is removed or its entity destroyed |

Entities tagged `Disabled` are skipped by every update hook.

Execution order between script types is set at registration: `scripts.add<T>(name, order)`,
lower runs first, default 0.

## ScriptContext

Every hook gets a `ScriptContext` for its own entity:

| Call | Does |
| --- | --- |
| `ctx.get<T>()` | Component on this entity (like `GetComponent<T>()`); must exist |
| `ctx.tryGet<T>()` | Pointer, `nullptr` if missing |
| `ctx.has<T>()` | Whether this entity has `T` |
| `ctx.add<T>(args...)` | Add or replace a component on this entity |
| `ctx.transform()` | Shortcut for `get<Transform>()` (local transform) |
| `ctx.destroy()` | Tags the entity `Dead`; destroyed with its children at the end of the fixed step |
| `ctx.defer(fn)` | Runs `fn(Scene&)` after the current hook phase |
| `ctx.scene`, `ctx.self`, `ctx.registry()` | Full access when needed |

Refer to other entities by `entt::entity`, never by pointer, and check
`ctx.registry().valid(e)` before using one.

## Attaching scripts

In code, scripts are added like any component:

```cpp
registry.emplace<Bobber>(duck);
registry.emplace<Rotator>(duckling, glm::vec3(1, 0, 0), 180.0f);  // aggregate init, in member order
```

In the editor, select an object and use *Add Script* in the inspector. Remove a script
with the × on its header.

## Rules

- **Don't add or remove the script's own type during its hooks**, on any entity. The
  runner is iterating that storage. Spawning an object that carries the same script
  goes through `ctx.defer`:

  ```cpp
  ctx.defer([](Scene& scene) {
      entt::entity e = scene.createGameObject("Spawned");
      scene.registry().emplace<Bobber>(e);
  });
  ```

- **Destroy through `ctx.destroy()`**, not `registry.destroy`, so nothing is
  destroyed while a loop runs and the hierarchy stays linked.
- **Every script needs at least one data member.** Empty structs are tags in EnTT and
  have no instance to call hooks on; this is a compile error with a clear message.
- **Inspector field types** are `float`, `int`, `bool`, `glm::vec3`, `std::string` and
  `entt::entity`. Anything else fails to compile at `.field<>()`.
- **Components must stay movable** (EnTT packs them tightly and moves them on removal),
  so don't keep pointers to `this` or to other components.

## Writing a system instead

Shared behaviour is a component plus a free function:

```cpp
// src/game/Components.h
struct Bob { float height = 0.5f, speed = 2.0f; };

// src/game/Systems.cpp
void bobSystem(entt::registry& registry, float dt)
{
    for (auto [e, transform, bob] : registry.view<Transform, Bob>(entt::exclude<Disabled>).each()) {
        // ...
    }
}
```

Call it from `Gameplay::fixedUpdate` in the order you want, and register the component
in `registerGameComponents` (`src/game/Components.cpp`) to edit it in the inspector:

```cpp
components.add<Bob>("Bob").field<&Bob::height>("Height", 0.01f);
```

Tips for hot loops:

- Put requirements in the view (`view<A, B>(entt::exclude<C>)`) instead of `has` /
  `try_get` checks inside the loop.
- Use empty tag structs (`Disabled`, `Dead`, `Player`) for yes/no state.
- Keep components small and split by use so each loop touches only what it needs.

## Reflection for your own components

`ComponentRegistry` (`src/engine/reflect/ComponentRegistry.h`) describes types for tools.
Plain members use `.field<&T::member>(label, speed)`. Members behind getters/setters, or
values that need converting, use `.property()`:

```cpp
components.add<Health>("Health")
    .property("Percent", FieldType::Float,
              [](const void* c) -> FieldValue { return static_cast<const Health*>(c)->ratio() * 100.0f; },
              nullptr);  // nullptr setter = read-only
```

`.locked()` hides a component from *Add Component* and its remove button. Set
`.addable = false` alone for components that need constructor data, like `MeshRenderer`.
