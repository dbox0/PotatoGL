#include "EventDemo.h"

#include "engine/script/ScriptRegistry.h"
#include "engine/script/ScriptRunner.h"

#include <cstdio>

REGISTER_SCRIPT(PingSender);
REGISTER_SCRIPT(PingLogger);

void PingSender::fields(ComponentInfo& info)
{
    info.field<&PingSender::mode>("mode").options({"Immediate", "Queued", "Both"})
        .field<&PingSender::offset>("offset")
        .field<&PingSender::tint>("tint", 0.01f).color();
}

void PingSender::onUpdate(ScriptContext& ctx, float)
{
    const Input& input = ctx.input();
    const bool down = input.isKeyDown(SDL_SCANCODE_P);
    if (down && !wasDown) {
        ++count;
        if (mode != PingMode::Queued) {
            std::printf("[PingSender] emit %d\n", count);
            ctx.emit(Ping{count, false});
        }
        if (mode != PingMode::Immediate) {
            std::printf("[PingSender] enqueue %d\n", count);
            ctx.enqueue(Ping{count, true});
        }
        ctx.emit(NamedEvent::make("Ping", static_cast<float>(count), 10.0f, ctx.self));
        std::printf("[PingSender] end of onUpdate\n");
    }
    wasDown = down;

    if (input.mousePressed(SDL_BUTTON_LEFT)) {
        const glm::vec2 p = input.mousePosition();
        std::printf("[PingSender] click at %.0f, %.0f\n", p.x, p.y);
    }
    if (input.mouseReleased(SDL_BUTTON_LEFT))
        std::printf("[PingSender] release\n");
    if (input.mouseWheel() != 0.0f)
        std::printf("[PingSender] wheel %.1f\n", input.mouseWheel());
}

void PingLogger::fields(ComponentInfo& info)
{
    info.field<&PingLogger::received>("received").notSaved();
}

void PingLogger::onStart(ScriptContext& ctx)
{
    ctx.on<Ping, &PingLogger::onPing>();
    ctx.on<NamedEvent, &PingLogger::onNamed>();
}

void PingLogger::onPing(ScriptContext&, const Ping& ping)
{
    ++received;
    std::printf("[PingLogger] got %s ping %d (total %d)\n", ping.queued ? "queued" : "immediate", ping.count,
                received);
}

void PingLogger::onNamed(ScriptContext&, const NamedEvent& event)
{
    if (event.name == NamedEvent::hash("Ping"))
        std::printf("[PingLogger] named Ping value %.0f/%.0f\n", event.value, event.max);
}
