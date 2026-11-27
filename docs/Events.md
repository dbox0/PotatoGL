# Events

`EventBus` (`src/engine/core/EventBus.h`) carries messages between scripts, systems and UI
without them knowing each other. It wraps `entt::dispatcher` and lives in the scene
registry's context, next to `Input`.

## Kinds of events

| | Typed | Named |
| --- | --- | --- |
| Written as | A plain struct in `src/game` | `NamedEvent` with a string name |
| Matched by | C++ type | Hash of the name |
| Carries | Any fields | `sender`, `value`, `max` |
| Use for | Gameplay code talking to gameplay code | Data UI components pick up by name |

```cpp
struct PlayerDamaged { float amount, current, max; };

NamedEvent::make("PlayerHealth", current, max);  // name, value, max, sender
```

`NamedEvent::name` is `entt::hashed_string` of the name. Compare with `NamedEvent::hash("PlayerHealth")`.

## Delivery

| Call | Listeners run |
| --- | --- |
| `emit(e)` | Now, before `emit` returns |
| `enqueue(e)` | At `flush()`, which App calls once per frame after the game update |

Stop clears the queue, so queued events never leak out of Play mode.

Listeners must not destroy entities or remove components while an event is being
delivered. Use `ctx.destroy()`, which is deferred.

## From scripts

```cpp
void Hud::onStart(ScriptContext& ctx)
{
    ctx.on<PlayerDamaged, &Hud::onDamaged>();
}

void Hud::onDamaged(ScriptContext& ctx, const PlayerDamaged& e) { ... }

ctx.emit(PlayerDamaged{10.0f, 90.0f, 100.0f});
ctx.enqueue(NamedEvent::make("PlayerHealth", 90.0f, 100.0f, ctx.self));
```

- Subscribe in `onStart`, not `onCreate`. `onCreate` also runs when a scene loads in Edit
  mode.
- The listener holds the entity, never `this`: scripts are components and EnTT moves them.
  Each event looks the script up again, and skips it while the entity is `Disabled`.
- The connections live in an `EventListeners` component (runtime only, never saved).
  Removing the script disconnects its own; destroying the entity disconnects all of them.

## From systems

```cpp
Connection c = bus.on<PlayerDamaged>([](const PlayerDamaged& e) { ... });
Connection c = bus.on<PlayerDamaged, &HudSystem::onDamaged>(hudSystem);
```

`Connection` disconnects when destroyed, so keep it as long as the subscription should
last. The member function form needs an instance with a stable address (a system or
other long-lived object), never a component.
