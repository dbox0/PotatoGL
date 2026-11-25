#pragma once

#include <string_view>
#include <vector>

class ScriptRunner;

struct ScriptRegistration
{
    std::string_view name;
    void (*add)(ScriptRunner&);
};

// Function-local static, so it's safe regardless of static init order across files
inline std::vector<ScriptRegistration>& scriptRegistrations()
{
    static std::vector<ScriptRegistration> registrations;
    return registrations;
}

struct ScriptRegistrar
{
    ScriptRegistrar(std::string_view name, void (*add)(ScriptRunner&))
    {
        scriptRegistrations().push_back({name, add});
    }
};

// Put in the script's .cpp (which must also include ScriptRunner.h). The game library is
// an OBJECT library, so the linker can't drop the registrar like it would in a static lib.
#define REGISTER_SCRIPT(Type)                                                             \
    static const ScriptRegistrar s_scriptRegistrar_##Type{#Type, [](ScriptRunner& runner) { \
        runner.add<Type>(#Type);                                                          \
    }}
