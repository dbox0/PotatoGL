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

struct ComponentInfo;
struct ScriptContext;

struct Bobber
{
    float height = 0.5f;
    float speed = 2.0f;
    float time = 0.0f;
    float baseY = 0.0f;

    void onStart(ScriptContext& ctx);
    void onUpdate(ScriptContext& ctx, float dt);
    static void fields(ComponentInfo& info);
};
```

```cpp
// src/game/scripts/Bobber.cpp
#include "Bobber.h"

#include "engine/script/ScriptRegistry.h"
#include "engine/script/ScriptRunner.h"

#include <cmath>

REGISTER_SCRIPT(Bobber);

void Bobber::fields(ComponentInfo& info)
{
    info.field<&Bobber::height>("height", 0.01f).field<&Bobber::speed>("speed", 0.05f);
}

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

That's all. `REGISTER_SCRIPT(Bobber)` registers the script at startup under its type
name, and the `game` library globs `src/game/`, so new files are picked up on the next
build without editing `CMakeLists.txt`. `game` is an `OBJECT` library on purpose: in a
static library the linker would drop the registrar, since nothing references it.

Fields listed in `fields()` show up in the inspector and are saved with the scene. The
first argument is the key, written as the member's name: scene files store it, so
renaming it breaks saved scenes. The inspector label is the nicified key (`height` ->
*Height*); pass a label second to override it (`("degreesPerSec", "Degrees/sec", 1.0f)`).
The last argument is the drag speed. Unlisted members (`time`, `baseY` above) are
internal state: not shown, not saved, reset to their defaults on load. `fields()` is
optional.

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

Execution order between script types comes from an optional `static constexpr int order`
on the struct, lower runs first, default 0. Types with the same order run by name.

## ScriptContext

Every hook gets a `ScriptContext` for its own entity:

| Call | Does |
| --- | --- |
| `ctx.get<T>()` | Component on this entity (like `GetComponent<T>()`); must exist |
| `ctx.tryGet<T>()` | Pointer, `nullptr` if missing |
| `ctx.has<T>()` | Whether this entity has `T` |
| `ctx.add<T>(args...)` | Add or replace a component on this entity |
| `ctx.transform()` | Local transform; looked up once per hook, then cached |
| `ctx.input()` | Keyboard state: `isKeyDown(SDL_SCANCODE_...)`; reports nothing while the editor has keyboard focus |
| `ctx.destroy()` | Tags the entity `Dead`; destroyed with its children at the end of the fixed step |
| `ctx.defer(fn)` | Runs `fn(Scene&)` after the current hook phase; captures must fit 64 bytes (no heap allocation) |
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
- **Field types** are `float`, `int`, `bool`, `glm::vec3`, `glm::quat`, `std::string`
  and `entt::entity`. Anything else fails to compile at `.field<>()`. Entity fields are
  remapped on load, so references between objects survive saving.
- **The script's type name is its id in scene files.** Renaming the struct breaks
  scenes that use it.
- **`onCreate` sees the saved values.** On load the component is filled in before it's
  added.
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
components.add<Bob>("Bob").field<&Bob::height>("height", 0.01f);
```

Tips for hot loops:

- Put requirements in the view (`view<A, B>(entt::exclude<C>)`) instead of `has` /
  `try_get` checks inside the loop.
- Use empty tag structs (`Disabled`, `Dead`, `Player`) for yes/no state.
- Keep components small and split by use so each loop touches only what it needs.

## Reflection for your own components

`ComponentRegistry` (`src/engine/reflect/ComponentRegistry.h`) describes types for the
inspector and the scene serializer. `components.add<T>("Id")` takes the id scene files
use; the display name is the nicified id (`MeshRenderer` -> *Mesh Renderer*), or set it
with `.label("...")`.

Plain members use `.field<&T::member>(key, speed)` or `.field<&T::member>(key, label, speed)`.
Members behind getters/setters, or values that need converting, use `.property()`:

```cpp
components.add<Health>("Health")
    .property("percent", "Percent", FieldType::Float,
              [](const void* c) -> FieldValue { return static_cast<const Health*>(c)->ratio() * 100.0f; },
              nullptr);  // nullptr setter = read-only, never saved
```

Modifiers apply to the field added last: `.notSaved()` keeps it out of scene files (like
Transform's Euler angles, which only the inspector uses), `.hidden()` keeps it out of the
inspector (like Transform's quaternion, which is what gets saved).

`.locked()` hides a component from *Add Component* and its remove button. Set
`.addable = false` alone for components that need constructor data.

Components that aren't registered (`WorldTransform`, `Selected`, `Dead`) are never saved.
