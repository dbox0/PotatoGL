#include "Components.h"

#include "engine/reflect/ComponentRegistry.h"

void registerGameComponents(ComponentRegistry& components)
{
    components.add<Spin>("Spin").field<&Spin::radiansPerSec>("Radians/sec", 0.05f);
    components.add<Velocity>("Velocity").field<&Velocity::value>("Value");
    components.add<Lifetime>("Lifetime").field<&Lifetime::remaining>("Remaining");
}
